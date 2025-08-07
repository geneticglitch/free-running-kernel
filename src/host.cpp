#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <fstream>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include "xrt/xrt_bo.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"

// FPGA Input Manager - Runs in separate thread
void input_manager_thread(
    std::atomic<bool>& stop_signal,
    xrt::run& read_run,
    xrt::bo& buffer_in,
    std::queue<int>& input_queue,
    std::mutex& input_mutex,
    std::condition_variable& input_cv
) {
    auto in_ptr = buffer_in.map<int*>();
    in_ptr[0] = 0;
    in_ptr[1] = 0;
    buffer_in.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    while (!stop_signal) {
        int next_input = 0;
        bool valid_input = false;

        // Get next input from queue
        {
            std::unique_lock<std::mutex> lock(input_mutex);
            input_cv.wait(lock, [&]{
                return !input_queue.empty() || stop_signal;
            });
            
            if (stop_signal) break;
            
            if (!input_queue.empty()) {
                next_input = input_queue.front();
                input_queue.pop();
                valid_input = true;
            }
        }

        if (valid_input) {
            // Send to FPGA
            in_ptr[0] = next_input;
            in_ptr[1] = 1;
            buffer_in.sync(XCL_BO_SYNC_BO_TO_DEVICE);

            // Wait for acknowledgment
            while (in_ptr[1] != 0) {
                buffer_in.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    // Send stop signal to FPGA
    in_ptr[1] = 2;
    buffer_in.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    read_run.wait();
}

// FPGA Output Manager - Runs in separate thread
void output_manager_thread(
    std::atomic<bool>& stop_signal,
    xrt::run& write_run,
    xrt::bo& buffer_out,
    std::queue<int>& result_queue,
    std::mutex& result_mutex,
    std::condition_variable& result_cv
) {
    auto out_ptr = buffer_out.map<int*>();
    out_ptr[0] = 1;   // Initial product
    out_ptr[1] = 0;   // Valid flag
    buffer_out.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    while (!stop_signal) {
        // Check for FPGA results
        buffer_out.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
        if (out_ptr[1] == 1) {
            int result = out_ptr[0];
            
            // Add to result queue
            {
                std::lock_guard<std::mutex> lock(result_mutex);
                result_queue.push(result);
            }
            result_cv.notify_one();

            // Reset flag
            out_ptr[1] = 0;
            buffer_out.sync(XCL_BO_SYNC_BO_TO_DEVICE);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Process final result
    buffer_out.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    if (out_ptr[1] == 1) {
        std::lock_guard<std::mutex> lock(result_mutex);
        result_queue.push(out_ptr[0]);
        result_cv.notify_one();
    }
    
    write_run.wait();
}

// File Writer - Runs in separate thread
void file_writer_thread(
    std::atomic<bool>& stop_signal,
    std::queue<int>& result_queue,
    std::mutex& result_mutex,
    std::condition_variable& result_cv
) {
    std::ofstream result_file("results.txt", std::ios::app);
    if (!result_file.is_open()) {
        std::cerr << "Error opening results file!" << std::endl;
        return;
    }

    while (true) {
        int result = 0;
        bool valid_result = false;

        // Get next result from queue
        {
            std::unique_lock<std::mutex> lock(result_mutex);
            result_cv.wait(lock, [&]{
                return !result_queue.empty() || stop_signal;
            });
            
            if (stop_signal && result_queue.empty()) break;
            
            if (!result_queue.empty()) {
                result = result_queue.front();
                result_queue.pop();
                valid_result = true;
            }
        }

        if (valid_result) {
            result_file << result << std::endl;
        }
    }

    result_file.close();
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <xclbin_file> <device_id>\n";
        return 1;
    }

    // Initialize FPGA
    auto device = xrt::device(std::stoi(argv[2]));
    auto uuid = device.load_xclbin(argv[1]);

    // Get kernels
    auto mem_read = xrt::kernel(device, uuid, "mem_read");
    auto mem_write = xrt::kernel(device, uuid, "mem_write");

    // Create buffers
    auto buffer_in = xrt::bo(device, sizeof(int) * 2, 
                            xrt::bo::flags::host_only, 
                            mem_read.group_id(0));
    
    auto buffer_out = xrt::bo(device, sizeof(int) * 2,
                             xrt::bo::flags::host_only,
                             mem_write.group_id(0));

    // Start FPGA kernels
    auto read_run = xrt::run(mem_read);
    read_run.set_arg(0, buffer_in);
    read_run.start();

    auto write_run = xrt::run(mem_write);
    write_run.set_arg(0, buffer_out);
    write_run.start();

    // Thread coordination
    std::atomic<bool> stop_signal(false);
    std::queue<int> input_queue;
    std::queue<int> result_queue;
    std::mutex input_mutex, result_mutex;
    std::condition_variable input_cv, result_cv;

    // Start worker threads
    std::thread input_thread(input_manager_thread, 
                            std::ref(stop_signal), 
                            std::ref(read_run),
                            std::ref(buffer_in),
                            std::ref(input_queue),
                            std::ref(input_mutex),
                            std::ref(input_cv));
    
    std::thread output_thread(output_manager_thread,
                             std::ref(stop_signal),
                             std::ref(write_run),
                             std::ref(buffer_out),
                             std::ref(result_queue),
                             std::ref(result_mutex),
                             std::ref(result_cv));
    
    std::thread writer_thread(file_writer_thread,
                             std::ref(stop_signal),
                             std::ref(result_queue),
                             std::ref(result_mutex),
                             std::ref(result_cv));

    // Main thread handles user input
    while (true) {
        std::cout << "Enter integer (q to quit): ";
        std::string input;
        std::getline(std::cin, input);
        
        if (input == "q") break;

        try {
            int val = std::stoi(input);
            
            // Add to input queue
            {
                std::lock_guard<std::mutex> lock(input_mutex);
                input_queue.push(val);
            }
            input_cv.notify_one();
            
        } catch (...) {
            std::cout << "Invalid input\n";
        }
    }

    // Shutdown sequence
    stop_signal = true;
    
    // Wake up all threads
    input_cv.notify_one();
    result_cv.notify_one();
    
    // Wait for threads to finish
    input_thread.join();
    output_thread.join();
    writer_thread.join();

    std::cout << "Application finished. Results saved to results.txt" << std::endl;
    return 0;
}
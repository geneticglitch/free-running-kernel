#include <ap_axi_sdata.h>
#include <ap_int.h>
#include <hls_stream.h>

extern "C" {
void mem_read(int* mem, hls::stream<ap_axiu<32, 0, 0, 0>>& stream) {
    #pragma HLS INTERFACE axis port=stream
    #pragma HLS INTERFACE m_axi port=mem offset=slave bundle=gmem
    volatile int* value_ptr = &mem[0];
    volatile int* flag_ptr = &mem[1];
    
    bool running = true;
    while (running) {
        #pragma HLS PIPELINE II=1
        if (*flag_ptr == 2) {
            ap_axiu<32, 0, 0, 0> v;
            v.data = -999;
            v.last = 0;
            stream.write(v);
            *flag_ptr = 0;
            running = false;
        }
        else if (*flag_ptr == 1) {
            int val = *value_ptr;
            ap_axiu<32, 0, 0, 0> v;
            v.data = val;
            v.last = 0;
            stream.write(v);
            *flag_ptr = 0;
        }
        
    }
}
}
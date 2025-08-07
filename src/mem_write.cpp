#include <ap_axi_sdata.h>
#include <ap_int.h>
#include <hls_stream.h>

extern "C" {
void mem_write(int* mem, hls::stream<ap_axiu<32, 0, 0, 0>>& stream) {
    #pragma HLS INTERFACE m_axi port=mem offset=slave bundle=gmem
    #pragma HLS INTERFACE axis port=stream
    
    volatile int* result_ptr = &mem[0];
    volatile int* flag_ptr = &mem[1];
    
    while (true) {
        #pragma HLS PIPELINE II=1
        
        if (*flag_ptr == 0 && !stream.empty()) {
            ap_axiu<32, 0, 0, 0> v = stream.read();
            
            if(v.last) {
                *result_ptr = v.data;
                *flag_ptr = 1; 
                break;
            }
            *result_ptr = v.data;
            *flag_ptr = 1;
        }
        
    }
}
}
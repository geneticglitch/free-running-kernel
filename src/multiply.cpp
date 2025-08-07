#include <ap_axi_sdata.h>
#include <ap_int.h>
#include <hls_stream.h>

extern "C" {
void multiply(hls::stream<ap_axiu<32, 0, 0, 0>>& input,
              hls::stream<ap_axiu<32, 0, 0, 0>>& output) {
    #pragma HLS interface ap_ctrl_none port=return
    #pragma HLS interface axis port=input
    #pragma HLS interface axis port=output
    
    static ap_uint<32> running_product = 1;
    
    while(true){
        #pragma HLS PIPELINE II=1
        if(!input.empty()) {
            ap_axiu<32, 0, 0, 0> v_in = input.read();
            ap_uint<32> new_value = v_in.data;
            ap_axiu<32, 0, 0, 0> v_out;

            if(v_in.data == static_cast<ap_uint<32> >(static_cast<uint32_t>(-999))) {
                v_out.last = 1;
            } else {
                v_out.last = 0;
            }

            if (new_value == 0 || new_value == -9999) {
                running_product = 1;
            } else {
                running_product *= new_value;
            }

            v_out.data = running_product;
            v_out.keep = 0xF;
            v_out.strb = 0xF;
            output.write(v_out);
        }
    }
}
}
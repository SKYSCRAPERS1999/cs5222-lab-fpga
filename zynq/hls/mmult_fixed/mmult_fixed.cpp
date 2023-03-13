#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "mmult.h"

// --------------------------------------------------------------------
// function to be accelerated in HW wrapped with AXI4-Stream interface
void mmult_hw (AXI_VAL in_stream[IS_SIZE], AXI_VAL out_stream[OS_SIZE])
{
#pragma HLS INTERFACE s_axilite port=return     bundle=CONTROL_BUS
#pragma HLS INTERFACE axis      port=in_stream
#pragma HLS INTERFACE axis      port=out_stream

	// Assertions (to avoid out of array bound writes)
	assert(BATCH%TILING==0);
	assert(FEAT%W_WIDTH_RATIO==0);
	assert(FEAT%IN_WIDTH_RATIO==0);
	assert((BATCH*CLASSES)%OUT_WIDTH_RATIO==0);

	// Hardware memory buffers
	out_T offset_buf[CLASSES];
	w_T weight_buf[CLASSES][FEAT];
	#pragma HLS ARRAY_PARTITION variable=weight_buf block factor=32 dim=2
	in_T in_buf[TILING][FEAT];
	#pragma HLS ARRAY_PARTITION variable=in_buf block factor=32 dim=2
	out_T out_buf[TILING][CLASSES];

	// Union used for type conversion
	union
	{
		axi_T packet;
		struct { int32_t val[2]; } val;
	} offset_converter, out_converter;

	union
	{
		axi_T packet;
		struct { int8_t val[8]; } val;
	} w_converter;

	union
	{
		axi_T packet;
		struct { uint8_t val[8]; } val;
	} in_converter;
	// Input and output AXI stream indices
	int is_idx = 0;
	int os_idx = 0;
	
	// Stream in offset vector
	// CSE548 TODO
	LOAD_OFF_1: for (int i = 0; i < CLASSES; i+=2) {
		offset_converter.packet = pop_stream(in_stream[is_idx++]);
		offset_buf[i+0] = offset_converter.val.val[0];
		offset_buf[i+1] = offset_converter.val.val[1];
	}
	// Stream in weight matrix
	// CSE548 TODO
	LOAD_W_1: for (int i = 0; i < CLASSES; i++) {
		LOAD_W_2: for (int j = 0; j < FEAT; j+=8) {
			// Pop AXI data packet
			w_converter.packet = pop_stream(in_stream[is_idx++]);
			weight_buf[i][j+0]  = w_converter.val.val[0];
			weight_buf[i][j+1]  = w_converter.val.val[1];
			weight_buf[i][j+2]  = w_converter.val.val[2];
			weight_buf[i][j+3]  = w_converter.val.val[3];
			weight_buf[i][j+4]  = w_converter.val.val[4];
			weight_buf[i][j+5]  = w_converter.val.val[5];
			weight_buf[i][j+6]  = w_converter.val.val[6];
			weight_buf[i][j+7]  = w_converter.val.val[7];
		}
	}
	// Iterate over tiles
	LT: for (int t = 0; t < BATCH; t+=TILING) {

		// Stream in input tile
		// CSE548 TODO
		LOAD_I_1: for (int i = 0; i < TILING; i++) {
			#pragma HLS PIPELINE II=1
			LOAD_I_2: for (int j = 0; j < FEAT; j+=8) {
				// Pop AXI data packet
				in_converter.packet = pop_stream(in_stream[is_idx++]);
				in_buf[i][j+0]  = in_converter.val.val[0];
				in_buf[i][j+1]  = in_converter.val.val[1];
				in_buf[i][j+2]  = in_converter.val.val[2];
				in_buf[i][j+3]  = in_converter.val.val[3];
				in_buf[i][j+4]  = in_converter.val.val[4];
				in_buf[i][j+5]  = in_converter.val.val[5];
				in_buf[i][j+6]  = in_converter.val.val[6];
				in_buf[i][j+7]  = in_converter.val.val[7];
			}
		}

		// Perform matrix multiplication
		L1: for (int i = 0; i < TILING; i++) {
			// Iterate over output classes
			L2: for (int j = 0; j < CLASSES; j++) {
				// Perform the dot product
				out_T tmp = offset_buf[j];
				#pragma HLS PIPELINE II=1
				L3: for(int k = 0; k < FEAT; k++) {
					out_T mult = in_buf[i][k] * weight_buf[j][k];
					tmp += mult;
				}
				out_buf[i][j] = tmp;
			}
		}

		// Stream out output matrix
		// CSE548 TODO
		STORE_O_1: for (int i = 0; i < TILING; i++) {
			STORE_O_2: for (int j = 0; j < CLASSES; j+=2) {
				// Push output element into AXI stream
				out_converter.val.val[0] = out_buf[i][j+0];
				out_converter.val.val[1] = out_buf[i][j+1];
				out_stream[os_idx++] = push_stream(out_converter.packet, os_idx == (OS_SIZE));
			}
		}
	}
}


// --------------------------------------------------------
// functions to insert and extract elements from an axi stream
// includes conversion to correct data type
axi_T pop_stream(AXI_VAL const &e)
{
#pragma HLS INLINE

	axi_T ret = e.data;

	volatile ap_uint<sizeof(axi_T)> strb = e.strb;
	volatile ap_uint<sizeof(axi_T)> keep = e.keep;
	volatile ap_uint<AXI_U> user = e.user;
	volatile ap_uint<1> last = e.last;
	volatile ap_uint<AXI_TI> id = e.id;
	volatile ap_uint<AXI_TD> dest = e.dest;

	return ret;
}

AXI_VAL push_stream(axi_T const &v, bool last = false)
{
#pragma HLS INLINE

	AXI_VAL e;

	e.data = v;
	e.strb = (1<<sizeof(axi_T))-1;
	e.keep = (1<<sizeof(axi_T))-1;
	e.user = 0;
	e.last = last ? 1 : 0;
	e.id = 0;
	e.dest = 0;
	return e;
}


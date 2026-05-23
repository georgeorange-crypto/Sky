#include <stdint.h>
#include <cstddef>
#include <stdlib.h>
#include <string.h>
/**
 * 将一个块中的double数据按字节拆分为8个lane
 * 每个lane包含所有double值的同一字节位置
 
void split_block_into_lanes(const double *data, size_t num_doubles,
                            vector<vector<uint8_t>> &lanes) {
  const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
  for (size_t i = 0; i < num_doubles; ++i) {
    for (int lane = 0; lane < 8; ++lane) {
      lanes[lane].push_back(raw[i * 8 + lane]);
    }
  }
}

*
 * 将8个lane的字节交错还原为double数据
 
void merge_lanes_to_block(const vector<vector<uint8_t>> &lanes,
                          size_t num_doubles, double *output) {
  uint8_t *raw = reinterpret_cast<uint8_t *>(output);
  for (size_t i = 0; i < num_doubles; ++i) {
    for (int lane = 0; lane < 8; ++lane) {
      raw[i * 8 + lane] = lanes[lane][i];
    }
  }
}*/

extern "C"{
void split_block_into_lanes( double *data, size_t num_doubles,
                            uint8_t *  lanes) {
  const uint8_t *raw = reinterpret_cast< uint8_t *>(data);
  size_t i = 0;
  size_t idx = 0;
  for (i = 0; i < num_doubles; ++i) {
    for (int lane = 0; lane < 8; ++lane) {
      //lanes[lane].push_back(raw[i * 8 + lane]);
      lanes[lane*num_doubles+i] = raw[i * 8 + lane];
           
      
    }
  }
}

void merge_lanes_to_block(uint8_t * lanes,
                          size_t num_doubles, double *output) {
  uint8_t *raw = reinterpret_cast<uint8_t *>(output);
  for (size_t i = 0; i < num_doubles; ++i) {
    for (int lane = 0; lane < 8; ++lane) {
      raw[i * 8 + lane] = lanes[lane*num_doubles+i];
    }
  }
 }


void split_block_into_lanes_u0( double *data, size_t num_doubles,
                            uint8_t *  lanes) {
  const uint64_t *raw = reinterpret_cast< uint64_t *>(data);
  size_t i = 0;
  for (i = 0; i < num_doubles; ++i) {
    uint64_t value = raw[i];
    lanes[i] = value & 0xFF; // 提取最低字节
    lanes[1*num_doubles+i] = (value >> 8) & 0xFF;
    lanes[2*num_doubles+i] = (value >> 16) & 0xFF;
    lanes[3*num_doubles+i] = (value >> 24) & 0xFF;
    lanes[4*num_doubles+i] = (value >> 32) & 0xFF;
    lanes[5*num_doubles+i] = (value >> 40) & 0xFF;
    lanes[6*num_doubles+i] = (value >> 48) & 0xFF;
    lanes[7*num_doubles+i] = (value >> 56) & 0xFF;
  }
}

void split_block_into_lanes_1( double *data, size_t num_doubles,
                            uint8_t *  lanes) {
  const uint64_t *raw = reinterpret_cast< uint64_t *>(data);
  uint64_t *lanes64 = reinterpret_cast< uint64_t *>(lanes);
  size_t cycle_num = num_doubles / 8;
  uint64_t *lanes64_0 = lanes64;
  uint64_t *lanes64_1 = lanes64 + cycle_num;
  uint64_t *lanes64_2 = lanes64 + 2 * cycle_num;
  uint64_t *lanes64_3 = lanes64 + 3 * cycle_num;
  uint64_t *lanes64_4 = lanes64 + 4 * cycle_num;
  uint64_t *lanes64_5 = lanes64 + 5 * cycle_num;
  uint64_t *lanes64_6 = lanes64 + 6 * cycle_num;
  uint64_t *lanes64_7 = lanes64 + 7 * cycle_num;
  size_t i = 0;
  for (i = 0; i < cycle_num; ++i) {
    uint64_t value0 = raw[i];
    uint64_t value1 = raw[i+1];
    uint64_t value2 = raw[i+2];
    uint64_t value3 = raw[i+3];
    uint64_t value4 = raw[i+4];
    uint64_t value5 = raw[i+5];
    uint64_t value6 = raw[i+6];
    uint64_t value7 = raw[i+7];
    lanes64_0[i] = (value0 & 0xFF) | ((value1 & 0xFF) << 8) | ((value2 & 0xFF) << 16) | ((value3 & 0xFF) << 24) |
                     ((value4 & 0xFF) << 32) | ((value5 & 0xFF) << 40) | ((value6 & 0xFF) << 48) | ((value7 & 0xFF) << 56);
    lanes64_1[i] = ((value0 >> 8) & 0xFF) | (((value1 >> 8) & 0xFF) << 8) | (((value2 >> 8) & 0xFF) << 16) | (((value3 >> 8) & 0xFF) << 24) |
                     (((value4 >> 8) & 0xFF) << 32) | (((value5 >> 8) & 0xFF) << 40) | (((value6 >> 8) & 0xFF) << 48) | (((value7 >> 8) & 0xFF) << 56);
    lanes64_2[i] = ((value0 >> 16) & 0xFF) | (((value1 >> 16) & 0xFF) << 8) | (((value2 >> 16) & 0xFF) << 16) | (((value3 >> 16) & 0xFF) << 24) |
                     (((value4 >> 16) & 0xFF) << 32) | (((value5 >> 16) & 0xFF) << 40) | (((value6 >> 16) & 0xFF) << 48) | (((value7 >> 16) & 0xFF) << 56);
    lanes64_3[i] = ((value0 >> 24) & 0xFF) | (((value1 >> 24) & 0xFF) << 8) | (((value2 >> 24) & 0xFF) << 16) | (((value3 >> 24) & 0xFF) << 24) |
                     (((value4 >> 24) & 0xFF) << 32) | (((value5 >> 24) & 0xFF) << 40) | (((value6 >> 24) & 0xFF) << 48) | (((value7 >> 24) & 0xFF) << 56);
    lanes64_4[i] = ((value0 >> 32) & 0xFF) | (((value1 >> 32) & 0xFF) << 8) | (((value2 >> 32) & 0xFF) << 16) | (((value3 >> 32) & 0xFF) << 24) |
                     (((value4 >> 32) & 0xFF) << 32) | (((value5 >> 32) & 0xFF) << 40) | (((value6 >> 32) & 0xFF) << 48) | (((value7 >> 32) & 0xFF) << 56);
    lanes64_5[i] = ((value0 >> 40) & 0xFF) | (((value1 >> 40) & 0xFF) << 8) | (((value2 >> 40) & 0xFF) << 16) | (((value3 >> 40) & 0xFF) << 24) |
                     (((value4 >> 40) & 0xFF) << 32) | (((value5 >> 40) & 0xFF) << 40) | (((value6 >> 40) & 0xFF) << 48) | (((value7 >> 40) & 0xFF) << 56);
    lanes64_6[i] = ((value0 >> 48) & 0xFF) | (((value1 >> 48) & 0xFF) << 8) | (((value2 >> 48) & 0xFF) << 16) | (((value3 >> 48) & 0xFF) << 24) |
                     (((value4 >> 48) & 0xFF) << 32) | (((value5 >> 48) & 0xFF) << 40) | (((value6 >> 48) & 0xFF) << 48) | (((value7 >> 48) & 0xFF) << 56);
    lanes64_7[i] = ((value0 >> 56) & 0xFF) | (((value1 >> 56) & 0xFF) << 8) | (((value2 >> 56) & 0xFF) << 16) | (((value3 >> 56) & 0xFF) << 24) |
                     (((value4 >> 56) & 0xFF) << 32) | (((value5 >> 56) & 0xFF) << 40) | (((value6 >> 56) & 0xFF) << 48) | (((value7 >> 56) & 0xFF) << 56);
  }
}



void split_block_into_lanes_2( double *data, size_t num_doubles,
                            uint8_t *  lanes) {
  const uint64_t *raw = reinterpret_cast< uint64_t *>(data);
  uint64_t *lanes64 = reinterpret_cast< uint64_t *>(lanes);
  size_t cycle_num = num_doubles / 8;
  uint64_t *lanes64_0 = lanes64;
  uint64_t *lanes64_1 = lanes64 + cycle_num;
  uint64_t *lanes64_2 = lanes64 + 2 * cycle_num;
  uint64_t *lanes64_3 = lanes64 + 3 * cycle_num;
  uint64_t *lanes64_4 = lanes64 + 4 * cycle_num;
  uint64_t *lanes64_5 = lanes64 + 5 * cycle_num;
  uint64_t *lanes64_6 = lanes64 + 6 * cycle_num;
  uint64_t *lanes64_7 = lanes64 + 7 * cycle_num;
  size_t i = 0;
  for (i = 0; i < cycle_num; ++i) {
    uint64_t offset = i << 3;
    uint64_t value0 = raw[offset];
    uint64_t value1 = raw[offset + 1];
    uint64_t value2 = raw[offset + 2];
    uint64_t value3 = raw[offset + 3];
    uint64_t value4 = raw[offset + 4];
    uint64_t value5 = raw[offset + 5];
    uint64_t value6 = raw[offset + 6];
    uint64_t value7 = raw[offset + 7];
    lanes64_0[i] = (value0 & 0xFF) | ((value1 & 0xFF) << 8) | ((value2 & 0xFF) << 16) | ((value3 & 0xFF) << 24) |
                     ((value4 & 0xFF) << 32) | ((value5 & 0xFF) << 40) | ((value6 & 0xFF) << 48) | ((value7 & 0xFF) << 56);
    lanes64_1[i] = ((value0 >> 8) & 0xFF) | (((value1 >> 8) & 0xFF) << 8) | (((value2 >> 8) & 0xFF) << 16) | (((value3 >> 8) & 0xFF) << 24) |
                     (((value4 >> 8) & 0xFF) << 32) | (((value5 >> 8) & 0xFF) << 40) | (((value6 >> 8) & 0xFF) << 48) | (((value7 >> 8) & 0xFF) << 56);
    lanes64_2[i] = ((value0 >> 16) & 0xFF) | (((value1 >> 16) & 0xFF) << 8) | (((value2 >> 16) & 0xFF) << 16) | (((value3 >> 16) & 0xFF) << 24) |
                     (((value4 >> 16) & 0xFF) << 32) | (((value5 >> 16) & 0xFF) << 40) | (((value6 >> 16) & 0xFF) << 48) | (((value7 >> 16) & 0xFF) << 56);
    lanes64_3[i] = ((value0 >> 24) & 0xFF) | (((value1 >> 24) & 0xFF) << 8) | (((value2 >> 24) & 0xFF) << 16) | (((value3 >> 24) & 0xFF) << 24) |
                     (((value4 >> 24) & 0xFF) << 32) | (((value5 >> 24) & 0xFF) << 40) | (((value6 >> 24) & 0xFF) << 48) | (((value7 >> 24) & 0xFF) << 56);
    lanes64_4[i] = ((value0 >> 32) & 0xFF) | (((value1 >> 32) & 0xFF) << 8) | (((value2 >> 32) & 0xFF) << 16) | (((value3 >> 32) & 0xFF) << 24) |
                     (((value4 >> 32) & 0xFF) << 32) | (((value5 >> 32) & 0xFF) << 40) | (((value6 >> 32) & 0xFF) << 48) | (((value7 >> 32) & 0xFF) << 56);
    lanes64_5[i] = ((value0 >> 40) & 0xFF) | (((value1 >> 40) & 0xFF) << 8) | (((value2 >> 40) & 0xFF) << 16) | (((value3 >> 40) & 0xFF) << 24) |
                     (((value4 >> 40) & 0xFF) << 32) | (((value5 >> 40) & 0xFF) << 40) | (((value6 >> 40) & 0xFF) << 48) | (((value7 >> 40) & 0xFF) << 56);
    lanes64_6[i] = ((value0 >> 48) & 0xFF) | (((value1 >> 48) & 0xFF) << 8) | (((value2 >> 48) & 0xFF) << 16) | (((value3 >> 48) & 0xFF) << 24) |
                     (((value4 >> 48) & 0xFF) << 32) | (((value5 >> 48) & 0xFF) << 40) | (((value6 >> 48) & 0xFF) << 48) | (((value7 >> 48) & 0xFF) << 56);
    lanes64_7[i] = ((value0 >> 56) & 0xFF) | (((value1 >> 56) & 0xFF) << 8) | (((value2 >> 56) & 0xFF) << 16) | (((value3 >> 56) & 0xFF) << 24) |
                     (((value4 >> 56) & 0xFF) << 32) | (((value5 >> 56) & 0xFF) << 40) | (((value6 >> 56) & 0xFF) << 48) | (((value7 >> 56) & 0xFF) << 56);
  }
}


void merge_lanes_to_block_1(uint8_t * lanes,
                          size_t num_doubles, double *output) {
  const uint64_t *raw = reinterpret_cast< uint64_t *>(output);
  size_t i = 0;
  for (i = 0; i < num_doubles; ++i) {
    uint64_t value = raw[i];
    lanes[i] = value & 0xFF; // 提取最低字节
    lanes[num_doubles + i] = (value >> 8) & 0xFF; // 提取次低字节
    lanes[2 * num_doubles + i] = (value >> 16) & 0xFF; // 提取第三字节
    lanes[3 * num_doubles + i] = (value >> 24) & 0xFF; // 提取第四字节
    lanes[4 * num_doubles + i] = (value >> 32) & 0xFF; // 提取第五字节
    lanes[5 * num_doubles + i] = (value >> 40) & 0xFF; // 提取第六字节
    lanes[6 * num_doubles + i] = (value >> 48) & 0xFF; // 提取第七字节
    lanes[7 * num_doubles + i] = (value >> 56) & 0xFF; // 提取最高字节
  }
}


void split_block_into_lanes_p_v0(double* data, size_t num_doubles, 
	uint8_t * lanes,  size_t start_index, size_t len )
{
	/* num_doubles = all doubles count, len = doubles count in this segment
	   data is the start offset of this buffer segment
	   lanes is the buffer ptr of total lanes
	*/
	uint64_t * raw = reinterpret_cast< uint64_t *>(data);
	uint64_t value = 0;
	for(int i = start_index; i< start_index+len ; i++){
		value = raw[i-start_index];
		//lanes[i] = raw[i/num_doubles + 8*(i%num_doubles)];
		lanes[i] = value&0xff;
		lanes[num_doubles +i] =   (value >> 8)&0xff;
		lanes[num_doubles*2 +i] = (value >> 16)&0xff;
		lanes[num_doubles*3 +i] = (value >> 24)&0xff;
		lanes[num_doubles*4 +i] = (value >> 32)&0xff;
		lanes[num_doubles*5 +i] = (value >> 40)&0xff;
		lanes[num_doubles*6 +i] = (value >> 48)&0xff;
		lanes[num_doubles*7 +i] = (value >> 56)&0xff;
		 
		
	}
}

void do_pre_split_p_v0(double * data, size_t num_doubles , uint8_t * lanes, 
	size_t start_index, size_t len){

   //uint8_t * lanes = (uint8_t*)malloc(num_doubles * sizeof(double));
   split_block_into_lanes_p_v0(data, num_doubles, lanes, start_index, len);
   //memcpy(data, lanes, (num_doubles*sizeof(double)));
   //free(lanes);

}



void do_pre_split_v0(double * data, size_t num_doubles , uint8_t * lanes){

   //uint8_t * lanes = (uint8_t*)malloc(num_doubles * sizeof(double));
   split_block_into_lanes_u0(data, num_doubles, lanes);
   //memcpy(data, lanes, (num_doubles*sizeof(double)));
   //free(lanes);

}

void do_post_merge_v0( uint8_t * lanes, size_t num_doubles, double * ori_data ){

   //double * ori_data = (double *)malloc(num_doubles * sizeof(double));
   merge_lanes_to_block_1(lanes, num_doubles, ori_data);
   //memcpy(lanes, ori_data, (num_doubles*sizeof(double)));
   //free(ori_data);

}


} 



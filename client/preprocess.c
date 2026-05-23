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


void split_block_into_lanes(const double *data, size_t num_doubles,
                            uint8_t * * lanes) {
  const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
  for (size_t i = 0; i < num_doubles; ++i) {
    for (int lane = 0; lane < 8; ++lane) {
      //lanes[lane].push_back(raw[i * 8 + lane]);
      lanes[lane][i] = (raw[i * 8 + lane];     
      
    }
  }
}
 

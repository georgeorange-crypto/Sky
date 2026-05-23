#include "CpuANSEncode.h"
#include "CpuANSDecode.h"
#include <sys/types.h>
     #include <sys/stat.h>
       #include <fcntl.h>
#include <unistd.h>

using namespace cpu_ans;

void compressFileWithANS(
		//const std::string& inputFilePath,//输入数据文件路径
		//const std::string& tempFilePath,//压缩后文件保存路径
		uint32_t filesize,
		uint8_t * input_buf,
		uint8_t * output_buf,
        uint32_t& batchSize,//原本数据规模
		uint32_t& compressedSize,//压缩后数据大小
		int precision
		) {
    // std::ifstream inputFile(inputFilePath, std::ios::binary | std::ios::ate);
    //std::streamsize fileSize = inputFile.tellg();
    std::streamsize fileSize = filesize;
    std::vector<uint8_t> fileData(fileSize);
    //inputFile.seekg(0, std::ios::beg);
    //inputFile.read(reinterpret_cast<char*>(fileData.data()), fileSize);//全部按照uint8_t读入
    //inputFile.close();

    //uint8_t* inPtrs = fileData.data();
    uint8_t* inPtrs = input_buf;
    uint32_t offset = 0;

    batchSize = fileSize;

    uint32_t* outCompressedSize = (uint32_t*)malloc(sizeof(uint32_t));
    uint8_t* encPtrs = (uint8_t*)malloc(getMaxCompressedSize(fileSize));
    ANSCoalescedHeader* headerOut = (ANSCoalescedHeader*)encPtrs;
    uint32_t maxNumCompressedBlocks;

    uint32_t maxUncompressedWords = fileSize / sizeof(ANSDecodedT);
    maxNumCompressedBlocks =
        (maxUncompressedWords + kDefaultBlockSize - 1) / kDefaultBlockSize;//一个batch的数据以kDefaultBlockSize作为基准划分数据，形成多个数据块
    
    uint4* table = (uint4*)malloc(sizeof(uint4) * kNumSymbols);
    uint32_t* tempHistogram = (uint32_t*)malloc(sizeof(uint32_t) * kNumSymbols);
    uint32_t uncoalescedBlockStride = getMaxBlockSizeUnCoalesced(kDefaultBlockSize);
    uint8_t* compressedBlocks_host = (uint8_t*)std::aligned_alloc(kBlockAlignment, sizeof(uint8_t) * maxNumCompressedBlocks * uncoalescedBlockStride);
    uint32_t* compressedWords_host = (uint32_t*)std::aligned_alloc(kBlockAlignment, sizeof(uint32_t) * maxNumCompressedBlocks);
    uint32_t* compressedWords_host_prefix = (uint32_t*)std::aligned_alloc(kBlockAlignment, sizeof(uint32_t) * maxNumCompressedBlocks);
    uint32_t* compressedWordsPrefix_host = (uint32_t*)std::aligned_alloc(kBlockAlignment, sizeof(uint32_t) * maxNumCompressedBlocks);
    //std::cout<<"encode start!"<<std::endl;
    double comp_time = 999999;
    for(int i = 0; i < 1; i ++){
    auto start = std::chrono::high_resolution_clock::now();  

    ansEncode(
        table,
        tempHistogram,
        precision,
        inPtrs,
        batchSize,
        encPtrs,
        outCompressedSize,
        headerOut,
        maxNumCompressedBlocks,
        uncoalescedBlockStride,
        compressedBlocks_host,
        compressedWords_host,
        compressedWords_host_prefix,
        compressedWordsPrefix_host);

    auto end = std::chrono::high_resolution_clock::now();
    if(comp_time > std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1e3)
      comp_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1e3;  
    }
    double c_bw = ( 1.0 * fileSize / 1e6 ) / ( (comp_time) * 1e-3 );  
    //std::cout << "comp   time " << std::fixed << std::setprecision(3) << comp_time << " ms B/W "   
      //            << std::fixed << std::setprecision(1) << c_bw << " MB/s " << std::endl;

    //std::ofstream outputFile(tempFilePath, std::ios::binary);
    auto blockWordsOut = headerOut->getBlockWords(maxNumCompressedBlocks);
    auto BlockDataStart = headerOut->getBlockDataStart(maxNumCompressedBlocks);
    
    int i = 0;
    for(; i < maxNumCompressedBlocks - 1; i ++){
    
      auto uncoalescedBlock = compressedBlocks_host + i * uncoalescedBlockStride;
      for(int j = 0; j < kWarpSize; ++j){
        auto warpStateOut = (ANSWarpState*)uncoalescedBlock;
        headerOut->getWarpStates()[i].warpState[j] = (warpStateOut->warpState[j]);
      }

      blockWordsOut[i] = uint2{
          (kDefaultBlockSize << 16) | compressedWords_host[i], 
          compressedWordsPrefix_host[i]};
    }
    auto uncoalescedBlock = compressedBlocks_host + i * uncoalescedBlockStride;
    for(int j = 0; j < kWarpSize; ++j){
      auto warpStateOut = (ANSWarpState*)uncoalescedBlock;
      headerOut->getWarpStates()[i].warpState[j] = (warpStateOut->warpState[j]);
    }
    
    uint32_t lastBlockWords = fileSize % kDefaultBlockSize;
    lastBlockWords = lastBlockWords == 0 ? kDefaultBlockSize : lastBlockWords;

    blockWordsOut[i] = uint2{
        (lastBlockWords << 16) | compressedWords_host[i], compressedWordsPrefix_host[i]};
    
    //outputFile.write(reinterpret_cast<const char*>(encPtrs), headerOut->getCompressedOverhead(maxNumCompressedBlocks));

    memcpy(&output_buf[offset], reinterpret_cast<const char*>(encPtrs),  headerOut->getCompressedOverhead(maxNumCompressedBlocks));
    offset += headerOut->getCompressedOverhead(maxNumCompressedBlocks);

    i = 0;
    for(; i < maxNumCompressedBlocks - 1; i ++){
    
      auto uncoalescedBlock = compressedBlocks_host + i * uncoalescedBlockStride;
      uint32_t numWords = compressedWords_host[i];

      uint32_t limitEnd = divUp(numWords, kBlockAlignment / sizeof(ANSEncodedT));

      auto inT = (const uint4*)(uncoalescedBlock + sizeof(ANSWarpState));
      auto outT = (uint4*)(BlockDataStart + compressedWordsPrefix_host[i]);

      //   __builtin_memcpy(outT, inT, limitEnd << 4);
      //outputFile.write(reinterpret_cast<const char*>(inT), limitEnd << 4);

    	memcpy(&output_buf[offset], reinterpret_cast<const char*>(inT),  limitEnd << 4);
    	offset += limitEnd << 4;
    }
    // uncoalescedBlock = compressedBlocks_host + i * uncoalescedBlockStride;

    uint32_t numWords = compressedWords_host[i];

    uint32_t limitEnd = divUp(numWords, kBlockAlignment / sizeof(ANSEncodedT));

    auto inT = (const uint4*)(uncoalescedBlock + sizeof(ANSWarpState));
    auto outT = (uint4*)(BlockDataStart + compressedWordsPrefix_host[i]);

    // __builtin_memcpy,(outT, inT, limitEnd << 4);
    //outputFile.write(reinterpret_cast<const char*>(inT), limitEnd << 4);
    memcpy(&output_buf[offset], reinterpret_cast<const char*>(inT),  limitEnd << 4);
    offset += limitEnd << 4;

    uint32_t outsize = *outCompressedSize;
    compressedSize = outsize;

    // outputFile.write(reinterpret_cast<const char*>(encPtrs), outsize*sizeof(uint8_t));
    //outputFile.close();
    free(outCompressedSize);
    free(encPtrs);
    free(table);
    free(tempHistogram);
    free(compressedBlocks_host);
    free(compressedWords_host);
    free(compressedWords_host_prefix);
    free(compressedWordsPrefix_host);
}


// TODO 
void decompressFileWithANS(
		//const std::string& tempFilePath,
		//const std::string& outputFilePath,
		uint8_t * input_buf,
		uint8_t ** poutput_buf,
	        uint32_t input_length,
		uint32_t *poutput_length,	
		int precision) {
    //std::ifstream inFile0(tempFilePath, std::ios::binary);
    std::vector<uint8_t> fileCompressedHead(32);
    memcpy(fileCompressedHead.data(), input_buf, 32);
    //inFile0.read(reinterpret_cast<char*>(fileCompressedHead.data()), 32);
    auto Header = (ANSCoalescedHeader*)fileCompressedHead.data();
    int totalCompressedSize = Header->getTotalCompressedSize();
    int batchSize = Header->getTotalUncompressedWords();
    //inFile0.close();
    //std::ifstream inFile1(tempFilePath, std::ios::binary);
    std::vector<uint8_t> fileCompressedData(totalCompressedSize);
    memcpy(fileCompressedData.data(), input_buf, totalCompressedSize);
    //inFile1.read(reinterpret_cast<char*>(fileCompressedData.data()), totalCompressedSize);
    //inFile1.close();
    uint8_t* decPtrs = (uint8_t*)malloc(sizeof(uint8_t)*(batchSize));
    uint32_t* symbol = (uint32_t*)std::aligned_alloc(kBlockAlignment, sizeof(uint32_t) * (1 << precision));
    uint32_t* pdf = (uint32_t*)std::aligned_alloc(kBlockAlignment, sizeof(uint32_t) * (1 << precision));
    uint32_t* cdf = (uint32_t*)std::aligned_alloc(kBlockAlignment, sizeof(uint32_t) * (1 << precision));
    //std::cout<<"decode start!"<<std::endl;
    double decomp_time = 999999;
    for(int i = 0; i < 1; i ++){
    auto start = std::chrono::high_resolution_clock::now();
    ansDecode(
        symbol,
        pdf,
        cdf,
        precision,
        fileCompressedData.data(),
        decPtrs);
    auto end = std::chrono::high_resolution_clock::now();  
    if(decomp_time > std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1e3)
        decomp_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1e3; 
    }
    double dc_bw = ( 1.0 * totalCompressedSize / 1e6 ) / ( (decomp_time) * 1e-3 );
    //std::cout << "decomp time " << std::fixed << std::setprecision(6) << (decomp_time) << " ms B/W "   
      //            << std::fixed << std::setprecision(1) << dc_bw << " MB/s" << std::endl;
    //std::ofstream outFile(outputFilePath, std::ios::binary);
    //outFile.write(reinterpret_cast<const char*>(decPtrs), batchSize*sizeof(uint8_t));
    //outFile.close();
    //free(decPtrs);
    //mayl use decPtrs directly
    *poutput_buf = decPtrs;
    *poutput_length = batchSize*sizeof(uint8_t);
    free(symbol);
    free(pdf);
    free(cdf);
    decPtrs = NULL;
}


extern "C"{
uint8_t *  pans_compress_data(uint8_t* in_buf, uint32_t fsize, uint32_t * out_size)
{
	uint32_t batchSize;
    uint32_t compressedSize;
    int precision = 10; 

    omp_set_num_threads(1);
    ///////////////////////////
    //uint8_t * input_buf= (uint8_t *)malloc(1024*1024*10);
    int fd = 0;
    uint32_t filesize ;
    //fd = open(src, O_RDONLY);
    //filesize = read(fd, input_buf, 1024*1024*10);
    filesize = fsize;
    //memcpy(in_buf, input_buf, filesize);
    uint8_t * output_buf= (uint8_t *)malloc(filesize*3/2);

    compressFileWithANS(
     
	//"dd.bin",   
	filesize,
	in_buf,
        output_buf,	
        batchSize,
        compressedSize,
        precision);
    * out_size = compressedSize;
    return output_buf;

}


uint8_t *  pans_decompress_data(uint8_t* in_buf, uint32_t fsize, uint32_t *out_size)
{

      uint8_t * input_buf = in_buf;
      uint8_t * output_buf = NULL;

      uint32_t oriSize;
      int precision = 10;

     omp_set_num_threads(1);
      decompressFileWithANS(
		//const std::string& tempFilePath,
		//const std::string& outputFilePath,
		input_buf,
		&output_buf,
	        fsize,
		&oriSize,	
	        precision);
      *out_size = oriSize;
      return output_buf;


 }
}

#if 0
int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input.file> <output.file>" << std::endl;
        return 1;
    }
    uint32_t batchSize;
    uint32_t compressedSize;
    int precision = 10;
   /* 
    compressFileWithANS(
        argv[1], argv[2],
        batchSize,
        compressedSize,
        precision);
	*/
    pans_compress_file(argv[1], argv[2]);
        // printf("compressedSize: %d\n", compressedSize);
    printf("compress ratio: %f\n", 1.0 * batchSize / compressedSize);
    std::cout << "Compression completed successfully." << std::endl;
    return 0;
}
#endif

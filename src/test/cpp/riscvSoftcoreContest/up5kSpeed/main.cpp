#include "../testbench.h"
#include "VUp5kSpeed.h"
#include "VUp5kSpeed_Up5kSpeed.h"
#include "VUp5kSpeed_Spram.h"
#include "VUp5kSpeed_SB_SPRAM256KA.h"

#include <fstream>
#include <iostream>
#include <functional>

using namespace std;

class SerialTx : public Agent {
    std::function<bool()> pin;
    int baudPeriod;
    ofstream logTraces;
public:
    SerialTx(std::function<bool()> pin, int baudPeriod) : pin(pin), baudPeriod(baudPeriod){
        logTraces.open ("log.txt");
    }
    int inFrame = false;
    int bitId = 8;
    uint64_t baudTimeout = 0;
    char buffer;
    uint64_t flushTimout = 0;
    virtual void preCycle(uint64_t time) {}
    virtual void postCycle(uint64_t time) {
        if(flushTimout != 0) flushTimout--;
        if(time < baudTimeout) return;
        if(bitId == 8){
            if(!pin()){
                bitId = 0;
                baudTimeout = time + baudPeriod*1.5;
                buffer = 0;
            }
        } else {
            buffer |= pin() << bitId;
            bitId++;
            baudTimeout = time + baudPeriod;
            if(bitId == 8) {
            	cout << buffer;
                cout.flush();
				logTraces << buffer;
				if(flushTimout == 0){
				    logTraces.flush();
				    flushTimout = 100000;
				}
            }
        }
    }
};

class SpiFlash : public Agent {
	CData *ss, *sclk, *mosi, *miso;
    enum  { IDLE, INSTRUCTION, CONFIG, READ_ADDRESS, READ_DUMMY, READ_DATA} state = IDLE;
    uint32_t counter;
    uint32_t address;
    uint32_t buffer;
    CData sclkOld;
public:
    uint8_t rom[1 << 24];
    SpiFlash(CData* ss, CData* sclk, CData* mosi, CData* miso) : ss(ss), sclk(sclk), mosi(mosi), miso(miso){}
    void loadBin(uint32_t address, const char* path){
        FILE *f = fopen(path, "r");
        fseek(f, 0, SEEK_END);
        uint32_t binSize = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t * bin = new uint8_t[binSize];
        fread(bin, 1, binSize, f);

        for(int i = 0;i < binSize;i++){
        	rom[address + i] = bin[i];
        }
    }
    virtual void preCycle(uint64_t time) {}
    virtual void postCycle(uint64_t time) {
		if(*ss){
			state = IDLE;
		} else {
	    	bool risingEdge =   *sclk && !sclkOld;
	    	bool fallingEdge = !*sclk && sclkOld;
			switch(state){
			case IDLE:
				state = INSTRUCTION;
				counter = 0;
				buffer = 0;
				break;
			case INSTRUCTION:
				if(risingEdge){
					buffer |= *mosi << (7-counter);
					counter++;
					if(counter == 8){
						switch(buffer){
						case 0x81:
							state = CONFIG;
							buffer = 0;
							counter = 0;
							break;
						case 0x0B:
							state = READ_ADDRESS;
							buffer = 0;
							counter = 0;
							break;
						}
					}
				}
				break;
			case CONFIG:
				if(risingEdge){
					buffer |= *mosi << (7-counter);
					counter++;
					if(counter == 8){
						assert(buffer == 0x83);
					}
				}
				break;
			case READ_ADDRESS:
				if(risingEdge){
					buffer |= *mosi << (23-counter);
					counter++;
					if(counter == 24){
						state = READ_DUMMY;
						address = buffer;
						counter = 0;
					}
				}
				break;
			case READ_DUMMY:
				if(risingEdge){
					counter++;
					if(counter == 8){
						state = READ_DATA;
						counter = 0;
					}
				}
				break;
			case READ_DATA:
				if(fallingEdge){
					*miso = (rom[address] >> (7-counter)) & 1;
					counter++;
					if(counter == 8){
						address++;
						counter = 0;
					}
				}
				break;
			}

		}

		sclkOld = *sclk;
    }
};

#define SYSTEM_CLK_HZ 12000000
#define SERIAL_BAUDRATE 115200
#define TIMESCALE uint64_t(1e12)

int main(int argc, char **argv) {
	cout << "Simulation start" << endl;
	Verilated::commandArgs(argc, argv);
	TESTBENCH<VUp5kSpeed> *tb = new TESTBENCH<VUp5kSpeed>(TIMESCALE/SYSTEM_CLK_HZ);
	auto serialTx = new SerialTx([=]() {return tb->dut->io_serialTx;}, TIMESCALE/SERIAL_BAUDRATE);
	tb->add(serialTx);
	auto spiFlash = new SpiFlash(&tb->dut->io_flash_ss, &tb->dut->io_flash_sclk, &tb->dut->io_flash_mosi, &tb->dut->io_flash_miso);
	spiFlash->loadBin(0x020000, FLASH_BOOTLOADER);
	#ifdef FLASH_BIN
	spiFlash->loadBin(0x030000, FLASH_BIN);
	#endif
	tb->add(spiFlash);


    #ifdef IRAM_BIN
    FILE *ram_binFile = fopen(IRAM_BIN, "r");
    fseek(ram_binFile, 0, SEEK_END);
    uint32_t ram_binSize = ftell(ram_binFile);
    fseek(ram_binFile, 0, SEEK_SET);
    uint8_t * ram_bin = new uint8_t[ram_binSize];
    fread(ram_bin, 1, ram_binSize, ram_binFile);

    uint8_t *ram0 = (uint8_t*)tb->dut->Up5kSpeed->system_iRam->mems_0->mem;
    uint8_t *ram1 = (uint8_t*)tb->dut->Up5kSpeed->system_iRam->mems_1->mem;
    for(int i = 0;i < ram_binSize;i++){
        switch(i&3){
            case 0: ram0[i/4*2 + 0] = ram_bin[i]; break;
            case 1: ram0[i/4*2 + 1] = ram_bin[i]; break;
            case 2: ram1[i/4*2 + 0] = ram_bin[i]; break;
            case 3: ram1[i/4*2 + 1] = ram_bin[i]; break;
        }
    }
    #endif

    tb->reset();
	while(!tb->done()) {
		tb->tick();
	} exit(EXIT_SUCCESS);

	cout << "Simulation end" << endl;
}

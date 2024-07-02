#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/kvm.h>
#include <thread>
#include <cstdio>
#include <sstream>
#include <string>
#include <map>
#include <semaphore.h>
#include <queue>
#include <utility>

using namespace std;

#define PDE64_PRESENT 1
#define PDE64_RW (1U << 1)
#define PDE64_USER (1U << 2)
#define PDE64_PS (1U << 7)

#define CR4_PAE (1U << 5)
#define CR0_PE 1u
#define CR0_MP (1U << 1)
#define CR0_ET (1U << 4)
#define CR0_NE (1U << 5)
#define CR0_WP (1U << 16)
#define CR0_AM (1U << 18)
#define CR0_PG (1U << 31)

#define EFER_LME (1U << 8)
#define EFER_LMA (1U << 10)

#define CONSOLE_PORT 0xE9
#define FILE_PORT 0x0278

#define OPEN_FILE "0"
#define CLOSE_FILE "1"
#define READ_FILE "2"
#define WRITE_FILE "3"

sem_t mutex;

struct vm {
    int kvm_fd;
    int vm_fd;
    int vcpu_fd;
    char *mem;
    struct kvm_run *kvm_run;
    long mem_size;
    long page_size;
};

struct vmArgs {
    string guestArg;
    int memoryArg;
    int pageArg;
    vector<string> fileArgs;
};

int init_vm(struct vm *vm, long mem_size, long page_size) {
    struct kvm_userspace_memory_region region;
    int kvm_run_mmap_size;

    vm->mem_size = mem_size;
    vm->page_size = page_size;

    vm->kvm_fd = open("/dev/kvm", O_RDWR);
    if(vm->kvm_fd < 0) {
        perror("open /dev/kvm");
        return -1;
    }

    vm->vm_fd = ioctl(vm->kvm_fd, KVM_CREATE_VM, 0);
    if(vm->vm_fd < 0) {
        perror("KVM_CREATE_VM");
        return -1;
    }

    vm->mem = (char*)mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if(vm->mem == MAP_FAILED) {
        perror("mmap mem");
        return -1;
    }

    region.slot = 0;
	region.flags = 0;
    region.guest_phys_addr = 0;
    region.memory_size = mem_size;
    region.userspace_addr = (unsigned long)vm->mem;
    if(ioctl(vm->vm_fd, KVM_SET_USER_MEMORY_REGION, &region) < 0) {
        perror("KVM_SET_USER_MEMORY_REGION");
        return -1;
    }

    vm->vcpu_fd = ioctl(vm->vm_fd, KVM_CREATE_VCPU, 0);
    if(vm->vcpu_fd < 0) {
        perror("KVM_CREATE_VCPU");
        return -1;
    }

    kvm_run_mmap_size = ioctl(vm->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    if(kvm_run_mmap_size <= 0) {
        perror("KVM_GET_VCPU_MMAP_SIZE");
        return -1;
    }

    vm->kvm_run = (struct kvm_run*)mmap(NULL, kvm_run_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vm->vcpu_fd, 0);
    if(vm->kvm_run == MAP_FAILED) {
        perror("mmap kvm_run");
        return -1;
    }

    return 0;
}

static void setup_64bit_code_segment(struct kvm_sregs *sregs) {
    struct kvm_segment seg = {
        0,              // base
        0xffffffff,     // limit
        0,              // selector
        11,             // type
        1,              // present
        0,              // dpl
        0,              // db
        1,              // s
        1,              // l
        1,              // g
        0,              // avl
        0,              // unusable
        0,              // padding
    };

    sregs->cs = seg;

    seg.type = 3;
    sregs->ds = sregs->es = sregs->fs = sregs->gs = sregs->ss = seg;
}

static void setup_long_mode(struct vm *vm, struct kvm_sregs *sregs) {
    long mem_size = vm->mem_size;
    long page_size = vm->page_size;

    uint64_t page = 0;
    uint64_t pml4_addr = 0x1000;
    uint64_t *pml4 = (uint64_t*)(vm->mem + pml4_addr);

    uint64_t pdpt_addr = 0x2000;
    uint64_t *pdpt = (uint64_t*)(vm->mem + pdpt_addr);

    uint64_t pd_addr = 0x3000;
    uint64_t *pd = (uint64_t*)(vm->mem + pd_addr);

    uint64_t pt_addr = 0x4000;
    uint64_t *pt = (uint64_t*)(vm->mem + pt_addr);

    pml4[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | pdpt_addr;
    pdpt[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | pd_addr;

    if(page_size == 2 * 1024 * 1024) {
        uint64_t num_entries = mem_size / (2 * 1024 * 1024);
        for (int i = 0; i < num_entries && i < 4; i++) {
            pd[i] = PDE64_PRESENT | PDE64_RW | PDE64_USER | PDE64_PS | page;
            page += 2 << 20;
        }
    } else if(page_size == 4 * 1024) {
        uint64_t number_of_2mbs = mem_size / (2 * 1024 * 1024);
        for(uint64_t i = 0; i < number_of_2mbs; i++) {
            pd[i] = PDE64_PRESENT | PDE64_RW | PDE64_USER | pt_addr;
            for(int j = 0; j < 512; j++) {
                pt[j] = page | PDE64_PRESENT | PDE64_RW | PDE64_USER;
                page += 0x1000;
		    }
            pt_addr += 0x1000;
        }
    }

    sregs->cr3 = pml4_addr;
    sregs->cr4 = CR4_PAE;
    sregs->cr0 = CR0_PE | CR0_PG;
    sregs->efer = EFER_LME | EFER_LMA;

    setup_64bit_code_segment(sregs);
}

vector<string> split(const string& str, char delimiter) {
    vector<string> tokens;
    string token;
    istringstream tokenStream(str);
    
    while(getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    
    return tokens;
}

void pushFileHandleToQueue(FILE *file, queue<uint64_t> &sendBack) {
    uint64_t leftHalf, rightHalf;
    leftHalf = ((uintptr_t) file >> 32) & 0xFFFFFFFF;
    rightHalf = (uintptr_t) file & 0xFFFFFFFF;
    sendBack.push(leftHalf);
    sendBack.push(rightHalf);
}

uintptr_t strToPtr(string str) {
    stringstream ss;
    uintptr_t ptrValue;
    ss << hex << str;
    ss >> ptrValue;
    return ptrValue;
}

void api(struct vm vm, vector<string> fileArgs, string guest) {
    int stop = 0;
    int ret = 0;
    char data;

    string operation = "";
    map<FILE *, string> fileNames;
    map<FILE *, string> modes;
    map<FILE *, long> cursors;
    map<FILE *, bool> fileCopied;
    queue<uint64_t> sendBack;

    while(stop == 0) {
        ret = ioctl(vm.vcpu_fd, KVM_RUN, 0);
        if(ret == -1) {
            cout << "KVM_RUN failed" << endl;
            return;
        }

        switch(vm.kvm_run->exit_reason) {
            case KVM_EXIT_IO:
                if(vm.kvm_run->io.direction == KVM_EXIT_IO_OUT && vm.kvm_run->io.port == CONSOLE_PORT) {
                    char *p = (char *)vm.kvm_run;
                    cout << *(p + vm.kvm_run->io.data_offset);
                } else if(vm.kvm_run->io.direction == KVM_EXIT_IO_IN && vm.kvm_run->io.port == CONSOLE_PORT) {
                    scanf("%c", &data);
                    char *data_in = (((char*)vm.kvm_run)+ vm.kvm_run->io.data_offset);
                    (*data_in) = data;
                } else if(vm.kvm_run->io.direction == KVM_EXIT_IO_OUT && vm.kvm_run->io.port == FILE_PORT) {
                    sem_wait(&mutex);
                    char *p = (char *)vm.kvm_run;
                    operation += *(p + vm.kvm_run->io.data_offset);
                    if(operation[operation.length() - 1] == '#' && operation[operation.length() - 1] == operation[operation.length() - 2]) {
                        vector<string> args = split(operation, '#');
                        operation = "";

                        bool foundFile = false;
                        string fileName = "";
                        if(args[0] == OPEN_FILE) fileName = args[1];
                        else if(args[0] == CLOSE_FILE) fileName = fileNames[reinterpret_cast<FILE *>(strToPtr(args[1]))];
                        else if(args[0] == READ_FILE || args[0] == WRITE_FILE) fileName = fileNames[reinterpret_cast<FILE *>(strToPtr(args[4]))];
                        
                        for(int i = 0; i < fileArgs.size(); i++) {
                            if(strcmp(fileArgs[i].c_str(), fileName.c_str()) == 0) {
                                foundFile = true;
                                break;
                            }
                        }
                        
                        if(foundFile) {
                            //shared files
                            if(args[0] == OPEN_FILE) {
                                FILE* file = fopen(args[1].c_str(), args[2].c_str());
                                fileNames[file] = args[1];
                                modes[file] = args[2];
                                fileCopied[file] = false;
                                pushFileHandleToQueue(file, sendBack);
                            } else if(args[0] == CLOSE_FILE) {
                                FILE *file = reinterpret_cast<FILE *>(strToPtr(args[1]));
                                uint64_t ret = fclose(file);
                                sendBack.push(ret);
                            } else if(args[0] == READ_FILE) {
                                FILE *file = reinterpret_cast<FILE *>(strToPtr(args[4]));
                                if(!fileCopied[file]) {
                                    //reading from shared file, before first write
                                    fseek(file, 0, cursors[file]);
                                }

                                char *buffer = new char[stoi(args[2]) * stoi(args[3])];
                                uint64_t readCnt = fread(buffer, stoi(args[2]), stoi(args[3]), file);
                                memcpy(vm.mem + strToPtr(args[1]), buffer, stoi(args[2]) * stoi(args[3]));
                                delete[] buffer;

                                if(!fileCopied[file]) {
                                    cursors[file] = ftell(file);
                                }
                                sendBack.push(readCnt);
                                pushFileHandleToQueue(file, sendBack);
                            } else if(args[0] == WRITE_FILE) {
                                FILE *file = reinterpret_cast<FILE *>(strToPtr(args[4]));
                                if(!fileCopied[file]) {
                                    //first write
                                    FILE* file2 = fopen((args[5] + fileNames[file]).c_str(), modes[file].c_str());
                                    fileNames[file2] = args[5] + fileNames[file];
                                    modes[file2] = modes[file];
                                    fileCopied[file2] = true;

                                    //copying file to folder with private files
                                    long cursorTemp = cursors[file];
                                    fseek(file, 0, SEEK_SET);
                                    char buffer[10];
                                    size_t bytesRead;
                                    while((bytesRead = fread(buffer, 1, 10, file)) > 0) {
                                        fwrite(buffer, 1, bytesRead, file2);
                                    }
                                    fseek(file, 0, cursorTemp);
                                    fseek(file2, 0, cursorTemp);
                                    fclose(file);

                                    char *buffer2 = new char[stoi(args[2]) * stoi(args[3])];
                                    memcpy(buffer2, vm.mem + strToPtr(args[1]), stoi(args[2]) * stoi(args[3]));
                                    uint64_t writeCnt = fwrite(buffer2, stoi(args[2]), stoi(args[3]), file2);
                                    delete[] buffer2;

                                    sendBack.push(writeCnt);
                                    pushFileHandleToQueue(file2, sendBack);
                                } else {
                                    char *buffer = new char[stoi(args[2]) * stoi(args[3])];
                                    memcpy(buffer, vm.mem + strToPtr(args[1]), stoi(args[2]) * stoi(args[3]));
                                    uint64_t writeCnt = fwrite(buffer, stoi(args[2]), stoi(args[3]), file);
                                    delete[] buffer;
                                    
                                    sendBack.push(writeCnt);
                                    pushFileHandleToQueue(file, sendBack);
                                }
                            }
                        } else {
                            //private files
                            if(args[0] == OPEN_FILE) {
                                FILE* file = fopen((args[3] + args[1]).c_str(), args[2].c_str());
                                fileNames[file] = args[3] + args[1];
                                modes[file] = args[2];
                                pushFileHandleToQueue(file, sendBack);
                            } else if(args[0] == CLOSE_FILE) {
                                FILE *file = reinterpret_cast<FILE *>(strToPtr(args[1]));
                                uint64_t ret = fclose(file);
                                sendBack.push(ret);
                            } else if(args[0] == READ_FILE) {
                                FILE *file = reinterpret_cast<FILE *>(strToPtr(args[4]));
                                char *buffer = new char[stoi(args[2]) * stoi(args[3])];
                                uint64_t readCnt = fread(buffer, stoi(args[2]), stoi(args[3]), file);
                                memcpy(vm.mem + strToPtr(args[1]), buffer, stoi(args[2]) * stoi(args[3]));
                                delete[] buffer;
                                sendBack.push(readCnt);
                                pushFileHandleToQueue(file, sendBack);
                            } else if(args[0] == WRITE_FILE) {
                                FILE *file = reinterpret_cast<FILE *>(strToPtr(args[4]));
                                char *buffer = new char[stoi(args[2]) * stoi(args[3])];
                                memcpy(buffer, vm.mem + strToPtr(args[1]), stoi(args[2]) * stoi(args[3]));
                                uint64_t writeCnt = fwrite(buffer, stoi(args[2]), stoi(args[3]), file);
                                delete[] buffer;
                                sendBack.push(writeCnt);
                                pushFileHandleToQueue(file, sendBack);
                            }
                        }
                    }
                    sem_post(&mutex);
                } else if(vm.kvm_run->io.direction == KVM_EXIT_IO_IN && vm.kvm_run->io.port == FILE_PORT) {
                    sem_wait(&mutex);
                    char *ptr = reinterpret_cast<char *>(vm.kvm_run) + vm.kvm_run->io.data_offset;
                    if(!sendBack.empty()) {
                        uint64_t value = sendBack.front();
                        sendBack.pop();
                        memcpy(ptr, &value, vm.kvm_run->io.size);
                    }
                    sem_post(&mutex);         
                }
                continue;
            case KVM_EXIT_HLT:
                cout << "KVM_EXIT_HLT" << endl;
                stop = 1;
                break;
            case KVM_EXIT_INTERNAL_ERROR:
                cout << "Internal error: suberror = 0x" << hex << vm.kvm_run->internal.suberror << endl;
                stop = 1;
                break;
            case KVM_EXIT_SHUTDOWN:
                cout << "Shutdown" << endl;
                stop = 1;
                break;
            default:
                cout << "Exit reason: " << vm.kvm_run->exit_reason << endl;
                break;
        }
    }
}

void vmRunner(struct vmArgs arg) {
    string guestArg = arg.guestArg;
    int memoryArg = arg.memoryArg;
    int pageArg = arg.pageArg;
    vector<string> fileArgs = arg.fileArgs;
    struct vm vm;
    struct kvm_sregs sregs;
    struct kvm_regs regs;
    ifstream img;

    long memorySize = memoryArg * 1024 * 1024;
    long pageSize;
    if(pageArg == 2) pageSize = 2 * 1024 * 1024;
    else if(pageArg == 4) pageSize = 4 * 1024;
    if(init_vm(&vm, memorySize, pageSize)) {
        cout << "Failed to init the VM" << endl;
        return;
    }

    if(ioctl(vm.vcpu_fd, KVM_GET_SREGS, &sregs) < 0) {
        perror("KVM_GET_SREGS");
        return;
    }

    setup_long_mode(&vm, &sregs);

    if(ioctl(vm.vcpu_fd, KVM_SET_SREGS, &sregs) < 0) {
        perror("KVM_SET_SREGS");
        return;
    }

    memset(&regs, 0, sizeof(regs));
    regs.rflags = 2;
    regs.rip = 0;
    regs.rsp = memorySize;

    if(ioctl(vm.vcpu_fd, KVM_SET_REGS, &regs) < 0) {
        perror("KVM_SET_REGS");
        return;
    }

    img.open(guestArg, ios::binary);
    if(!img.is_open()) {
        cout << "Can not open binary file" << endl;
        return;
    }

    char *p = vm.mem;
    while(!img.eof()) {
        img.read(p, 1024);
        p += img.gcount();
    }
    img.close();

    api(vm, fileArgs, guestArg);
}

bool parseArgs(int argc, char *argv[], int *memoryArg, int *pageArg, vector<string> &guestArgs, vector<string> &fileArgs) {
    for(int i = 1; i < argc; ) {
        if(strcmp(argv[i], "--memory") == 0 || strcmp(argv[i], "-m") == 0)  {
            if(strcmp(argv[i + 1], "2") && strcmp(argv[i + 1], "4") && strcmp(argv[i + 1], "8")) return false;
            *memoryArg = atoi(argv[i + 1]);
            i += 2;
        } else if(strcmp(argv[i], "--page") == 0 || strcmp(argv[i], "-p") == 0) {
            if(strcmp(argv[i + 1], "2") && strcmp(argv[i + 1], "4")) return false;
            *pageArg = atoi(argv[i + 1]);
            i += 2;
        } else if(strcmp(argv[i], "--guest") == 0 || strcmp(argv[i], "-g") == 0) {
            i++;
            while(i < argc) {
                if(strcmp(argv[i], "--memory") == 0 || strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--page") == 0 || strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--file") == 0 || strcmp(argv[i], "-f") == 0) break;
                guestArgs.emplace_back(argv[i]);
                i++;
            }
        } else if(strcmp(argv[i], "--file") == 0 || strcmp(argv[i], "-f") == 0) {
            i++;
            while(i < argc) {
                if(strcmp(argv[i], "--memory") == 0 || strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--page") == 0 || strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--guest") == 0 || strcmp(argv[i], "-g") == 0) break;
                fileArgs.emplace_back(argv[i]);
                i++;
            }
        } else {
            return false;
        }
    }
    return true;
}

int main(int argc, char *argv[]) {
    if(argc < 7) {
        cout << "Run program like this ./mini_hypervisor [--memory or -m] [2, 4 or 8] [--page or -p] [2 or 4] [--guest or -g] guest1/guest1.img guest2/guest2.img [--file or -f] lorem1.txt lorem2.txt" << endl;
        return 1;
    }

    sem_init(&mutex, 0, 1);

    int memoryArg;
    int pageArg;
    vector<string> guestArgs;
    vector<string> fileArgs;
    if(!parseArgs(argc, argv, &memoryArg, &pageArg, guestArgs, fileArgs)) {
        cout << "Run program like this ./mini_hypervisor [--memory or -m] [2, 4 or 8] [--page or -p] [2 or 4] [--guest or -g] guest1/guest1.img guest2/guest2.img [--file or -f] lorem1.txt lorem2.txt" << endl;
        return 1;
    }

    for(size_t i = 0; i < guestArgs.size(); i++) {
        for(size_t j = i + 1; j < guestArgs.size(); j++) {
            if(guestArgs[i] == guestArgs[j]) {
                cout << "You can't run the same guest more than once" << endl;
                return 1;
            }
        }
    }

    struct vmArgs vmArgs;
    vmArgs.memoryArg = memoryArg;
    vmArgs.pageArg = pageArg;
    vmArgs.fileArgs = fileArgs;

    vector<thread> threads;
    for(const auto& guestArg : guestArgs) {
        vmArgs.guestArg = guestArg;
        threads.emplace_back(vmRunner, vmArgs);
    }

    for(auto& thread : threads) {
        thread.join();
    }

    sem_destroy(&mutex);

    return 0;
}
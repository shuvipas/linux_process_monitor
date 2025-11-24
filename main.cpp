#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>
#include <unistd.h>
#define IS_DIGIT(a) ((a)>='0' && (a)<='9')

namespace fs = std::filesystem;
struct Process {
    pid_t pid;
    uid_t uid;
    std::string cmd;
    size_t mem_kb;
    // double cpu_percent;
    // double gpu_percent;  
};

struct MemTotal{
    size_t total_mem;
    double tot_mem_gb;
    size_t free_mem;
    size_t available_mem;
    int used_mem_percent;

};



bool readProcPid(std::string pid_str,Process& proc){
    // Check if directory name is a number (PID)
    proc.pid = 0;
    for (char c : pid_str) {
        if (!IS_DIGIT(c)) return false;
        proc.pid = proc.pid*10 + (c -'0');
    }
    return true;
}
void readProcCmd(std::string pid_str,Process& proc){
    std::string comm_path = "/proc/" + pid_str + "/comm";
    std::ifstream comm_file(comm_path);
    if (! std::getline(comm_file, proc.cmd)) {
        proc.cmd = "unknown";
    }
}

void readProcUid(std::string pid_str,Process& proc){
        proc.uid = -1;
        std::string status_path = "/proc/" + pid_str + "/status";
        std::ifstream status_file(status_path);
        std::string line;
        
        while (std::getline(status_file, line)) {
            if (line.find("Uid:") == 0) {
                // std::istringstream iss(line.substr(4));
                // iss >> proc.uid;
                sscanf(line.c_str(), "Uid: %d", &proc.uid);
                break;
            }
        }
}


void readProcMem(std::string pid_str,Process& proc){
    std::string statm_path = "/proc/" + pid_str + "/statm";
    std::ifstream statm_file(statm_path);
    size_t pages;
    statm_file >> pages;  
    proc.mem_kb = pages * (sysconf(_SC_PAGESIZE) / 1024);
}
void readTotMem(MemTotal& mem){
    std::ifstream meminfo("/proc/meminfo"); // mem info in kb
    std::string line;
    // size_t total_mem = 0, free_mem = 0, available_mem = 0;

    while (std::getline(meminfo, line)) {
        if (line.find("MemTotal:") == 0) {
            sscanf(line.c_str(), "MemTotal: %lu kB", &mem.total_mem);
        } else if (line.find("MemAvailable:") == 0) {
            sscanf(line.c_str(), "MemAvailable: %lu kB", &mem.available_mem);
        }
    }
    mem.tot_mem_gb = (double) mem.total_mem / (1024.0*1024.0);  
    mem.used_mem_percent = 100.0 * (1.0  -(double) mem.available_mem / mem.total_mem);
}
void scanProcesses(std::vector<Process>& processes) {
    processes.clear();
    
    for (const auto& entry : fs::directory_iterator("/proc")) {
        if (!entry.is_directory()) continue;
        Process curr_proc;
        std::string pid_str = entry.path().filename();
        
        if(!readProcPid(pid_str ,curr_proc)) continue;
        
        readProcCmd(pid_str ,curr_proc);

        readProcUid(pid_str ,curr_proc);
        readProcMem(pid_str, curr_proc);

        // // Calculate CPU usage
        // double cpu_percent = calculateCpuUsage(pid);
        
        processes.push_back(curr_proc);
    }
}

void displayProcesses(const std::vector<Process>& processes,MemTotal& mem ) {
    std::cout <<"System RAM: " << mem.tot_mem_gb 
    << " Gb\nUsed mem percent: " << mem.used_mem_percent << "%\n\n";
    std::cout <<"Found "<<processes.size() <<" processes\n";
    std::cout << "PID\tUID\tRAM\t\tCOMMAND\n";
    std::cout << "---\t---\t----\t\t-------\n";
    // if(limit && processes.size() < limit) limit =  processes.size(); 
    int limit =10;
    for (int i = 0; i < processes.size() &&i <limit ; i++) {
        const auto& p = processes[i];
        std::cout << p.pid << "\t" 
                  << p.uid << "\t"
                  << p.mem_kb <<"\t\t"
                  << p.cmd << "\n";
    }
    
    // if (processes.size() > limit) {
    //     std::cout << "... and " << (processes.size() - limit) << " more processes\n";
    // }
}


int main(){
    std::vector<Process> processes;
    MemTotal mem;
    readTotMem(mem);
    scanProcesses(processes);
    displayProcesses(processes,mem);
    return 0;
}
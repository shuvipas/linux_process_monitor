#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>
#include <unistd.h>

#include <unordered_map>
#define IS_DIGIT(a) ((a)>='0' && (a)<='9')

namespace fs = std::filesystem;
struct Process {
    pid_t pid =0;
    uid_t uid;
    std::string cmd;
    size_t mem_kb;
    
    double cpu_percent;
    size_t utime;
    size_t stime;
    size_t last_time;
  
};

struct SysInfo{
    size_t total_mem = 0;
    double tot_mem_gb = 0;
    size_t free_mem = 0;
    size_t available_mem = 0;
    double used_mem_percent = 0;

    unsigned long long cpu_prev_total_time = 0;
    std::unordered_map<pid_t, unsigned long long> cpu_prev_process_times;

};
SysInfo my_system;

// Read system-wide total CPU time from /proc/stat
unsigned long long readSystemCpuTime() {
    std::ifstream stat_file("/proc/stat");
    std::string line;
    std::getline(stat_file, line);
    
    unsigned long long user, nice, system, idle;
    sscanf(line.c_str(), "cpu %llu %llu %llu %llu", &user, &nice, &system, &idle);
    return user + nice + system + idle; 
}

// Read process CPU times from /proc/[PID]/stat
void readProcCpuTimes(std::string pid_str, Process& proc) {
    std::string stat_path = "/proc/" + pid_str + "/stat";
    std::ifstream stat_file(stat_path);
    std::string line;
    
    if (std::getline(stat_file, line)) {
        // Parse utime (field 14) and stime (field 15)
        // Note: fields are 1-indexed in the file
        std::istringstream iss(line);
        std::string token;
        
        // Skip first 13 fields
        for (int i = 0; i < 13; i++) iss >> token;
        
        // Read utime (14) and stime (15)
        iss >> proc.utime >> proc.stime;
    }
}

// Calculate CPU percentage for all processes
void calculateCpuPercentages(std::vector<Process>& processes) {
    unsigned long long current_total_time = readSystemCpuTime();
    
    if (my_system.cpu_prev_total_time > 0) {
        unsigned long long total_delta = current_total_time - my_system.cpu_prev_total_time;
        
        for (auto& proc : processes) {
            unsigned long long current_proc_time = proc.utime + proc.stime;
            
            if (my_system.cpu_prev_process_times.count(proc.pid)) {
                unsigned long long proc_delta = current_proc_time - my_system.cpu_prev_process_times[proc.pid];
                proc.cpu_percent = (total_delta > 0) ? 
                    (100.0 * proc_delta / total_delta) : 0.0;
            }
            
            my_system.cpu_prev_process_times[proc.pid] = current_proc_time;
        }
    }
    
    my_system.cpu_prev_total_time = current_total_time;
}






/*-----------------------------------------------------------------------------*/


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
void readTotMem(){
    std::ifstream meminfo("/proc/meminfo"); // mem info in kb
    std::string line;
    
    while (std::getline(meminfo, line)) {
        if (line.find("MemTotal:") == 0) {
            sscanf(line.c_str(), "MemTotal: %lu kB", &my_system.total_mem);
        } else if (line.find("MemAvailable:") == 0) {
            sscanf(line.c_str(), "MemAvailable: %lu kB", &my_system.available_mem);
        }
    }
    my_system.tot_mem_gb = (double) my_system.total_mem / (1024.0*1024.0);  
    if(my_system.total_mem) my_system.used_mem_percent = 100.0 * (1.0  -(double) my_system.available_mem / (my_system.total_mem));
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
        readProcCpuTimes(pid_str, curr_proc);
        
        processes.push_back(curr_proc);
    }
}

void displayProcesses(const std::vector<Process>& processes) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout <<"System RAM: " << my_system.tot_mem_gb 
    << " Gb\nUsed mem percent: " << my_system.used_mem_percent << "%\n\n";
    std::cout <<"Found "<<processes.size() <<" processes\n";
    std::cout << "PID\tUID\tRAM\t\tCPU\t\tCOMMAND\n";
    std::cout << "---\t---\t----\t\t-------\t\t-------\n";
  
    for (int i = 0; i < processes.size(); i++) {
        const auto& p = processes[i];
        std::cout << p.pid << "\t" 
                  << p.uid << "\t"
                  << p.mem_kb <<"\t\t"
                  <<p.cpu_percent<<"\t\t"
                  << p.cmd << "\n";
    }
    
    // if (processes.size() > limit) {
    //     std::cout << "... and " << (processes.size() - limit) << " more processes\n";
    // }
}


int main(){
    std::vector<Process> processes;
    
    readTotMem();
    scanProcesses(processes);
    calculateCpuPercentages(processes);
    displayProcesses(processes);
    return 0;
}
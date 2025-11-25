#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>
#include <unistd.h>

#include <unordered_map>
#include <algorithm>
#include <set>
#define IS_DIGIT(a) ((a)>='0' && (a)<='9')


using cpu_ticks = unsigned long long;
namespace fs = std::filesystem;
struct Process {
    pid_t pid = 0;
    uid_t uid;
    std::string cmd;
    size_t mem_kb = 0;
    
    double cpu_percent = 0.0;
    size_t utime = 0;
    size_t stime = 0;
    size_t last_time = 0;
  
};

struct SysInfo{
    size_t total_mem = 0;
    double tot_mem_gb = 0;
    size_t free_mem = 0;
    size_t available_mem = 0;
    double used_mem_percent = 0.0;
    double used_mem_gb = 0.0;
    double total_cpu_percent = 0.0;
    int page_size = 0;

    cpu_ticks cpu_prev_tot_time = 0;
    cpu_ticks cpu_prev_idle_time = 0;
    std::unordered_map<pid_t, cpu_ticks> cpu_prev_process_times;
    int core_number = 0;

};
SysInfo my_system;

// Read system-wide total CPU time from /proc/stat
void readSystemCpuTime(cpu_ticks& curr_tot_time, cpu_ticks& curr_idle_time) {
    std::ifstream stat_file("/proc/stat");
    std::string line;
    std::getline(stat_file, line);
    
    cpu_ticks user, nice, system, idle, iowait;
    sscanf(line.c_str(), "cpu %llu %llu %llu %llu %llu", 
           &user, &nice, &system, &idle, &iowait);
   
    curr_tot_time = user + nice + system + idle + iowait;
    curr_idle_time = idle + iowait;
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
void calculateCpuPercentages(std::unordered_map<pid_t,Process>& processes) {
    cpu_ticks curr_tot_time;
    cpu_ticks curr_idle_time;
    readSystemCpuTime(curr_tot_time, curr_idle_time);

    if (my_system.cpu_prev_tot_time > 0 && my_system.cpu_prev_idle_time > 0) {
        cpu_ticks total_delta = curr_tot_time - my_system.cpu_prev_tot_time;
        cpu_ticks idle_delta = curr_idle_time - my_system.cpu_prev_idle_time;
       
        if (total_delta > 0) {
            my_system.total_cpu_percent = 100.0 * (total_delta - idle_delta) / total_delta;
        }

        for (auto& [pid, proc] : processes) {
            cpu_ticks current_proc_time = proc.utime + proc.stime;
            
            if (my_system.cpu_prev_process_times.count(pid)) {
                cpu_ticks prev_time = my_system.cpu_prev_process_times[pid];
                if (current_proc_time > prev_time && total_delta > 0) {
                    cpu_ticks proc_delta = current_proc_time - prev_time;
                    proc.cpu_percent = (100.0 * proc_delta / total_delta);
                }
            }
            my_system.cpu_prev_process_times[pid] = current_proc_time;
        }
    }  else {
        // First run - just store baseline
        for (auto& [pid, proc] : processes) {
            my_system.cpu_prev_process_times[pid] = proc.utime + proc.stime;
        }
    }
    
    
    my_system.cpu_prev_tot_time = curr_tot_time;
    my_system.cpu_prev_idle_time = curr_idle_time;

}

bool readProcPid(std::string pid_str,pid_t & pid){
    // Check if directory name is a number (PID)
    // proc.pid = 0;
    for (char c : pid_str) {
        if (!IS_DIGIT(c)) return false;
        pid = pid*10 + (c -'0');
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

/*
void readProcMem(std::string pid_str,Process& proc){
    std::string statm_path = "/proc/" + pid_str + "/statm";
    std::ifstream statm_file(statm_path);
    size_t pages;
    statm_file >> pages; 
    std::string line;
    std::getline(statm_file, line);
    std::cout << "DEBUG statm line: " << line << std::endl; 
    std::cout << "DEBUG readProcMem: " << pid_str 
              << " | pages=" << pages 
              << " | page_size=" << my_system.page_size 
              << " | raw_calc=" << (pages * my_system.page_size) 
              << " | final_kb=" << ((pages * my_system.page_size) / 1024.0) 
              << " | final_mb=" << ((pages * my_system.page_size) / (1024.0 * 1024.0)) 
              << std::endl;
    proc.mem_kb = (pages * my_system.page_size)/1024.0;
}
*/

void readProcMem(std::string pid_str, Process& proc){
    std::string status_path = "/proc/" + pid_str + "/status";
    std::ifstream status_file(status_path);
    std::string line;
    
    while (std::getline(status_file, line)) {
        if (line.find("VmRSS:") == 0) {
            sscanf(line.c_str(), "VmRSS: %lu kB", &proc.mem_kb);
            break;
        }
    }
}

void readTotMem(){
    std::ifstream meminfo("/proc/meminfo"); // mem info in kb
    std::string line;
    
    while (std::getline(meminfo, line)) {
        if (line.find("MemAvailable:") == 0) {
            sscanf(line.c_str(), "MemAvailable: %lu kB", &my_system.available_mem);
            my_system.used_mem_gb = (double) (my_system.total_mem - my_system.available_mem) / (1024.0*1024.0); 
            if(my_system.total_mem) my_system.used_mem_percent = 100.0 * (1.0  -(double) my_system.available_mem / (my_system.total_mem));
        }
    }
    
}
void readSystemInfo() {
    my_system.page_size = (double) sysconf(_SC_PAGESIZE);
    // Read total memory
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    
    while (std::getline(meminfo, line)) {
        if (line.find("MemTotal:") == 0) {
            sscanf(line.c_str(), "MemTotal: %lu kB", &my_system.total_mem);
            my_system.tot_mem_gb = (double)my_system.total_mem / (1024.0 * 1024.0);
            break;
        }
    }
    
    std::ifstream cpuinfo("/proc/cpuinfo"); 
    while (std::getline(cpuinfo, line)) {
        if (line.find("processor") == 0) { 
            my_system.core_number++;
        }
    }  
   
}
 
void scanProcesses(std::unordered_map<pid_t,Process>& processes) {
    // processes.clear();
    std::set<pid_t> current_pids;
    for (const auto& entry : fs::directory_iterator("/proc")) {
        if (!entry.is_directory()) continue;
        
        std::string pid_str = entry.path().filename();
        pid_t curr_pid = 0;
        if(!readProcPid(pid_str ,curr_pid)) continue;
        current_pids.insert(curr_pid);

        auto it = processes.find(curr_pid);
        if (it == processes.end()) {
            // New process - insert and initialize static fields
            Process new_proc;
            new_proc.pid = curr_pid;
            readProcCmd(pid_str, new_proc);
            readProcUid(pid_str, new_proc);
            it = processes.insert({curr_pid, new_proc}).first;
        }
        Process& curr_proc = it->second;
        readProcMem(pid_str, curr_proc);
        readProcCpuTimes(pid_str, curr_proc);

    }
    // Remove dead processes
    for (auto it = processes.begin(); it != processes.end(); ) {
        if (current_pids.find(it->first) == current_pids.end()) {
            it = processes.erase(it);
        } else {
            ++it;
        }
    }
}


void displaySystemInfo(){
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "System Resources: "<<"RAM: " << my_system.tot_mem_gb << " GB | "
          << my_system.core_number<<" CPU cores\nRAM used:" 
          << my_system.used_mem_percent << "% ("<< my_system.used_mem_gb<<" GB) used \n"
          << "CPU: " << my_system.total_cpu_percent << "%  used\n";
}

void displayProcesses(const std::unordered_map<pid_t, Process>& processes_map) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Found " << processes_map.size() << " processes\n\n";
    
    // Convert map to vector for sorting
    std::vector<const Process*> sorted_processes;
    for (const auto& [pid, proc] : processes_map) {
        sorted_processes.push_back(&proc);
    }
    
    std::sort(sorted_processes.begin(), sorted_processes.end(), //highest Ram usage first
              [](const Process* a, const Process* b) { 
                  return a->mem_kb > b->mem_kb; 
              });
    
    std::cout << std::left 
              << std::setw(8) << "PID" 
              << std::setw(8) << "UID" 
              << std::setw(10) << "RAM(GB)" 
              << std::setw(8) << "CPU%" 
              << "COMMAND\n";
    
    std::cout << std::string(45, '-') << "\n";
    
    int limit = (int)sorted_processes.size();// std::min(15, (int)sorted_processes.size());
    for (int i = 0; i < limit; i++) {
        const Process* p = sorted_processes[i];
        std::cout << std::left 
                  << std::setw(8) << p->pid 
                  << std::setw(8) << p->uid 
                  << std::setw(10) << (((double) p->mem_kb) / (1024.0 * 1024.0)) 
                  << std::setw(8) << p->cpu_percent 
                  << p->cmd << "\n";
        // std::cout << "DEBUG: PID " << p->pid << " mem_kb=" << p->mem_kb 
        //   << " calculated_GB=" << (((double) p->mem_kb) / (1024.0*1024.0)) << "\n";
    }
    
    if (processes_map.size() > limit) {
        std::cout << "... and " << (processes_map.size() - limit) << " more processes\n";
    }
}
void displayMain(const std::unordered_map<pid_t, Process>& processes_map) {
    
    std::cout << "\033[2J\033[1;1H";  // clear screen
    
    displaySystemInfo();
    std::cout << "\n";  // Add spacing
    displayProcesses(processes_map);
}



int main(){
    std::unordered_map<pid_t, Process> processes;
    readSystemInfo();
    
    readTotMem();
    scanProcesses(processes);
    calculateCpuPercentages(processes);
    sleep(1);
    readTotMem();
    scanProcesses(processes);
    calculateCpuPercentages(processes);
    displayMain(processes);
    return 0;
}
#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>
#include <unistd.h>
#include <csignal>
#include <atomic>
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

    int gpu_memory_mb = 0.0;
};

struct ProcessGroup {
    std::string group_name;
    int count = 0;
    double cpu_percent = 0.0;
    size_t total_mem_kb = 0;
    double tot_mem_gb = 0;

    int gpu_memory_mb = 0.0;  
};
struct GpuInfo {
    std::string name;
    double total_vram_gb = 0.0;
    double total_vram_mb = 0.0;
    std::string driver_version;
    bool available = false;

    double gpu_utilization = 0.0;
    double memory_utilization = 0.0; 
    int memory_used_mb = 0.0;  
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
    std::unordered_map<pid_t, cpu_ticks> cpu_prev_process_times;  // Keep pid_t for individual processes
    int core_number = 0;

    size_t uid_min = 0;
    size_t uid_max = 0;

    GpuInfo gpu_info;
};

SysInfo my_system;
std::atomic<bool> keep_running{true};

/*
void readGpuProcesses(std::unordered_map<pid_t, Process>& processes) {
    FILE* pipe = popen("nvidia-smi --query-compute-apps=pid,used_memory --format=csv,noheader,nounits 2>/dev/null", "r");
    if (!pipe) return;
    
    char buffer[1024];
    for (auto& [pid, proc] : processes) {
        proc.gpu_memory_mb = 0;
    }
    
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line(buffer);
        
        if (!line.empty() && line.back() == '\n') {
            line.pop_back();
        }
        
        // Parse: "1234, 512" (PID, memory in MB)
        size_t pos = line.find(',');
        if (pos != std::string::npos) {
            std::string pid_str = line.substr(0, pos);
            std::string memory_str = line.substr(pos + 1);
            
            // Trim whitespace
            pid_str.erase(0, pid_str.find_first_not_of(" \t"));
            pid_str.erase(pid_str.find_last_not_of(" \t") + 1);
            memory_str.erase(0, memory_str.find_first_not_of(" \t"));
            memory_str.erase(memory_str.find_last_not_of(" \t") + 1);
            
            try {
                pid_t pid = std::stoi(pid_str);
                double gpu_memory = std::stod(memory_str);
                auto it = processes.find(pid);
                if (it != processes.end()) {
                    it->second.gpu_memory_mb = gpu_memory;
                }
            } catch (...) {
                // Skip invalid entries
            }
        }
    }
    pclose(pipe);
}
*/

void readGpuProcesses(std::unordered_map<pid_t, Process>& processes) {
    // Use the main nvidia-smi output that shows all processes
    FILE* pipe = popen("nvidia-smi 2>/dev/null", "r");
    if (!pipe) return;
    
    char buffer[1024];
    
    // Reset GPU memory for all processes
    for (auto& [pid, proc] : processes) {
        proc.gpu_memory_mb = 0;
    }
    
    bool in_processes_section = false;
    int processes_found = 0;
    
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line(buffer);
        
        if (!line.empty() && line.back() == '\n') {
            line.pop_back();
        }
        
        // Look for the processes section
        if (line.find("Processes:") != std::string::npos) {
            in_processes_section = true;
            continue;
        }
        
        // Skip the header line
        if (in_processes_section && line.find("GPU") == 0 && line.find("PID") != std::string::npos) {
            continue;
        }
        
        // Look for the end of processes section
        if (in_processes_section && line.find("+---") != std::string::npos) {
            break;
        }
        
        // Parse process lines in the processes section
        if (in_processes_section && !line.empty()) {
            size_t pid_start = line.find("N/A");
            if (pid_start == std::string::npos) continue;
            pid_start = line.find_first_of("0123456789", pid_start);
            if (pid_start == std::string::npos) continue;
            
            size_t pid_end = line.find_first_not_of("0123456789", pid_start);
            if (pid_end == std::string::npos) continue;
            
            std::string pid_str = line.substr(pid_start, pid_end - pid_start);
            size_t mem_pos = line.find("MiB");
            if (mem_pos == std::string::npos) continue;
            size_t mem_start = line.find_last_not_of("0123456789", mem_pos - 1);
            if (mem_start == std::string::npos) continue;
            
            mem_start++; // Move past the non-digit character
            std::string memory_str = line.substr(mem_start, mem_pos - mem_start);
            
            // Trim whitespace
            pid_str.erase(0, pid_str.find_first_not_of(" \t"));
            pid_str.erase(pid_str.find_last_not_of(" \t") + 1);
            memory_str.erase(0, memory_str.find_first_not_of(" \t"));
            memory_str.erase(memory_str.find_last_not_of(" \t") + 1);
            
            try {
                pid_t pid = std::stoi(pid_str);
                int gpu_memory = std::stoi(memory_str);
                
                auto it = processes.find(pid);
                if (it != processes.end()) {
                    it->second.gpu_memory_mb = gpu_memory;
                    processes_found++;
                }
            } catch (...) {
                // Skip invalid entries
            }
        }
    }
    pclose(pipe);
}

void readGpuDynamicInfo() {
    FILE* pipe = popen("nvidia-smi --query-gpu=utilization.gpu,memory.used --format=csv,noheader,nounits 2>/dev/null", "r");
    if (!pipe) return;
    
    char buffer[1024];
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line(buffer);
        
        if (!line.empty() && line.back() == '\n') {
            line.pop_back();
        }
        
        // Parse: "30, 5120"
        size_t pos = line.find(',');
        if (pos != std::string::npos) {
            // GPU utilization
            std::string gpu_util_str = line.substr(0, pos);
            gpu_util_str.erase(0, gpu_util_str.find_first_not_of(" \t"));
            gpu_util_str.erase(gpu_util_str.find_last_not_of(" \t") + 1);
            try {
                my_system.gpu_info.gpu_utilization = std::stod(gpu_util_str);
            } catch (...) {
                my_system.gpu_info.gpu_utilization = 0.0;
            }
            
            // Memory used
            std::string memory_used_str = line.substr(pos + 1);
            memory_used_str.erase(0, memory_used_str.find_first_not_of(" \t"));
            memory_used_str.erase(memory_used_str.find_last_not_of(" \t") + 1);
            try {
                my_system.gpu_info.memory_used_mb = std::stod(memory_used_str);
            } catch (...) {
                my_system.gpu_info.memory_used_mb = 0.0;
            }
            
            // Calculate memory utilization
            if (my_system.gpu_info.total_vram_gb > 0) {
                my_system.gpu_info.memory_utilization = 
                    (my_system.gpu_info.memory_used_mb / my_system.gpu_info.total_vram_mb) * 100.0;
            }
        }
    }
    pclose(pipe);
}

void readGpuStaticInfo() {
    // Try to execute nvidia-smi command
    FILE* pipe = popen("nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv,noheader,nounits 2>/dev/null", "r");
    if (!pipe) {
        my_system.gpu_info.available = false;
        return;
    }
    
    char buffer[1024];
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line(buffer);
        
        if (!line.empty() && line.back() == '\n') {
            line.pop_back();
        }
        
        size_t pos1 = line.find(',');
        size_t pos2 = line.find(',', pos1 + 1);
        
        if (pos1 != std::string::npos && pos2 != std::string::npos) {
            my_system.gpu_info.name = line.substr(0, pos1);
            
            // Extract total VRAM (in MB, convert to GB)
            std::string vram_str = line.substr(pos1 + 1, pos2 - pos1 - 1);
            // Remove whitespace
            vram_str.erase(0, vram_str.find_first_not_of(" \t"));
            vram_str.erase(vram_str.find_last_not_of(" \t") + 1);
            
            try {
                my_system.gpu_info.total_vram_mb = std::stod(vram_str);
                my_system.gpu_info.total_vram_gb = my_system.gpu_info.total_vram_mb / 1024.0;
            } catch (...) {
                my_system.gpu_info.total_vram_gb = 0.0;
            }
            my_system.gpu_info.driver_version = line.substr(pos2 + 1);
            
            my_system.gpu_info.driver_version.erase(0, my_system.gpu_info.driver_version.find_first_not_of(" \t"));
            my_system.gpu_info.driver_version.erase(my_system.gpu_info.driver_version.find_last_not_of(" \t") + 1);
            
            my_system.gpu_info.available = true;
        } else {
            my_system.gpu_info.available = false;
        }
    } else {
        my_system.gpu_info.available = false;
    }
    
    pclose(pipe);
}



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

void readProcCpuTimes(std::string pid_str, Process& proc) {
    std::string stat_path = "/proc/" + pid_str + "/stat";
    std::ifstream stat_file(stat_path);
    std::string line;
    
    if (std::getline(stat_file, line)) {
        std::istringstream iss(line);
        std::string token;
        // Skip first 13 fields
        for (int i = 0; i < 13; i++) iss >> token;        
        // Read utime (14) and stime (15)
        iss >> proc.utime >> proc.stime;
    }
}

// Calculate CPU percentage for individual processes
void calculateCpuPercentages(std::unordered_map<pid_t, Process>& processes) {
    cpu_ticks curr_tot_time, curr_idle_time;
    readSystemCpuTime(curr_tot_time, curr_idle_time);

    if (my_system.cpu_prev_tot_time > 0) {
        cpu_ticks total_delta = curr_tot_time - my_system.cpu_prev_tot_time;
        cpu_ticks idle_delta = curr_idle_time - my_system.cpu_prev_idle_time;
        
        if (total_delta > 0) {
            my_system.total_cpu_percent = 100.0 * (total_delta - idle_delta) / total_delta;
        }

        // Calculate individual process CPU percentages
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
    } else {
        // First run - just store baseline
        for (auto& [pid, proc] : processes) {
            my_system.cpu_prev_process_times[pid] = proc.utime + proc.stime;
        }
    }
    
    my_system.cpu_prev_tot_time = curr_tot_time;
    my_system.cpu_prev_idle_time = curr_idle_time;
}

bool readProcPid(std::string pid_str, pid_t& pid){
    pid = 0;
    for (char c : pid_str) {
        if (!IS_DIGIT(c)) return false;
        pid = pid * 10 + (c - '0');
    }
    return true;
}

void readProcCmd(std::string pid_str, std::string& proc_cmd){
    std::string comm_path = "/proc/" + pid_str + "/comm";
    std::ifstream comm_file(comm_path);
    if (!std::getline(comm_file, proc_cmd)) {
        proc_cmd = "unknown";
    }
}

uid_t readProcUid(std::string pid_str){
    uid_t uid = -1;
    std::string status_path = "/proc/" + pid_str + "/status";
    std::ifstream status_file(status_path);
    std::string line;
    
    while (std::getline(status_file, line)) {
        if (line.find("Uid:") == 0) {
            sscanf(line.c_str(), "Uid: %d", &uid);
            break;
        }
    }
    return uid;
}

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
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    
    while (std::getline(meminfo, line)) {
        if (line.find("MemAvailable:") == 0) {
            sscanf(line.c_str(), "MemAvailable: %lu kB", &my_system.available_mem);
            my_system.used_mem_gb = (double)(my_system.total_mem - my_system.available_mem) / (1024.0 * 1024.0); 
            if(my_system.total_mem) {
                my_system.used_mem_percent = 100.0 * (1.0 - (double)my_system.available_mem / my_system.total_mem);
            }
        }
    }
}

void readSystemInfo() {
    my_system.page_size = sysconf(_SC_PAGESIZE);
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
    
    std::ifstream login_defs("/etc/login.defs");
    while (std::getline(login_defs, line)) {
        if (line.empty() || line[0] == '#') continue;
        line.erase(0, line.find_first_not_of(" \t"));
        
        if (line.find("UID_MIN") == 0) {
            sscanf(line.c_str(), "UID_MIN %lu", &my_system.uid_min);
        } else if (line.find("UID_MAX") == 0) {
            sscanf(line.c_str(), "UID_MAX %lu", &my_system.uid_max);
        }
    }
    readGpuStaticInfo();
}

void assignGroupName(uid_t uid,const Process& proc, std::string& group_name){
    if(uid < my_system.uid_min || uid > my_system.uid_max){
        group_name = "system_processes";
    } else {
        group_name = proc.cmd;
    }
}


void formGroups(const std::unordered_map<pid_t, Process>& processes, 
                std::unordered_map<std::string, ProcessGroup>& groups) {
    groups.clear();
    
    for (const auto& proc_pair : processes) {
        const Process& proc = proc_pair.second; 
        std::string group_name;
        assignGroupName(proc.uid, proc, group_name);
        
        ProcessGroup& group = groups[group_name];
        group.group_name = group_name;
        group.count++;
        group.total_mem_kb += proc.mem_kb;
        group.tot_mem_gb += proc.mem_kb/(1024.0 * 1024.0);
        group.cpu_percent += proc.cpu_percent;
        group.gpu_memory_mb += proc.gpu_memory_mb;
    }
}


void scanProcesses(std::unordered_map<pid_t, Process>& processes) {
    std::set<pid_t> current_pids;
    
    for (const auto& entry : fs::directory_iterator("/proc")) {
        if (!entry.is_directory()) continue;
        
        std::string pid_str = entry.path().filename();
        pid_t curr_pid = 0;
        if(!readProcPid(pid_str, curr_pid)) continue;
        
        current_pids.insert(curr_pid);
        
        auto it = processes.find(curr_pid);
        if (it == processes.end()) {
            // New process
            Process new_proc;
            new_proc.pid = curr_pid;
            new_proc.uid = readProcUid(pid_str);
            readProcCmd(pid_str, new_proc.cmd);
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
    std::cout << "System Resources: RAM: " << my_system.tot_mem_gb << " GB | "
          << my_system.core_number << " CPU cores\n";
    if (my_system.gpu_info.available) {
        std::cout << "GPU: " << my_system.gpu_info.name;
        if (my_system.gpu_info.total_vram_gb > 0) {
            std::cout << " | VRAM: " << my_system.gpu_info.total_vram_mb << " MiB ("
            <<my_system.gpu_info.total_vram_gb<<" GB)";
        }
        std::cout << " | Driver Version: " << my_system.gpu_info.driver_version << "\n\n";
    } else {
        std::cout << "GPU: No NVIDIA GPU detected\n\n";
    }
          
    std::cout <<"RAM used: "<< my_system.used_mem_percent << "% (" << my_system.used_mem_gb 
    << " GB) used \n"<< "CPU: " << my_system.total_cpu_percent << "% used\n";
    
    if (my_system.gpu_info.available) {
        std::cout << "GPU: VRAM  "<< my_system.gpu_info.memory_used_mb 
                  << " MiB ("<< my_system.gpu_info.memory_utilization << "%) used | "
                  << "UTILIZATION: " << my_system.gpu_info.gpu_utilization << "%\n";
    }
}

void displayProcGroups(const std::unordered_map<std::string, ProcessGroup>& groups) {
    std::cout << std::fixed << std::setprecision(2);
    std::vector<const ProcessGroup*> sorted_groups;
    for (const auto& [name, group] : groups) {
        sorted_groups.push_back(&group);
    }
    
    std::sort(sorted_groups.begin(), sorted_groups.end(),
              [](const ProcessGroup* a, const ProcessGroup* b) { 
                  return (a->tot_mem_gb + a->cpu_percent+ (a->gpu_memory_mb/2)) >(b->tot_mem_gb + b->cpu_percent+(b->gpu_memory_mb/2)); 
              });

    std::cout << std::left 
              << std::setw(20) << "GROUP" 
              << std::setw(10) << "PROCESSES" 
              << std::setw(12) << "RAM(GB)" 
              << std::setw(8) << "CPU%"
              << std::setw(12) << "GPU MEM(MB)" 
              << "\n";
    
    std::cout << std::string(62, '-') << "\n";
    
    for (const auto* group : sorted_groups) {
        if(group->cpu_percent>0.1||group->tot_mem_gb > 0.1 || group->gpu_memory_mb)
        std::cout << std::left 
                  << std::setw(20) << (group->group_name.length() > 19 ? group->group_name.substr(0, 19) : group->group_name)
                  << std::setw(10) << group->count 
                  << std::setw(12) << group->tot_mem_gb
                  << std::setw(8) << group->cpu_percent
                  << std::setw(12) << (group->gpu_memory_mb > 0.1 ? std::to_string(group->gpu_memory_mb) : "")
                  << "\n";
    }
}

void displayMain(const std::unordered_map<std::string, ProcessGroup>& groups) {
    system("clear");
    std::cout << "\nProcess Monitor Running... Press Ctrl+C to exit\n\n";
    displaySystemInfo();
    std::cout << "\n";
    displayProcGroups(groups);
}
void signalHandler(int signal) {
    if (signal == SIGINT) {
        keep_running = false;
    }
}
int main(){
    std::unordered_map<pid_t, Process> processes;  
    std::unordered_map<std::string, ProcessGroup> groups;  
    std::signal(SIGINT, signalHandler);
    readSystemInfo();
    readGpuStaticInfo();
    std::cout << "Process Monitor Running... Press Ctrl+C to exit\n";
    
    while (keep_running) {
        readTotMem();
        readGpuDynamicInfo();
        scanProcesses(processes);
        readGpuProcesses(processes);

        calculateCpuPercentages(processes);
        formGroups(processes, groups);
        
        displayMain(groups);
        
        // Wait before next update (adjust sleep time as needed)
        sleep(1);
    }
    
    std::cout << "\nProcess Monitor stopped.\n";
    return 0;
}
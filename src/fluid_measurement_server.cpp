#include <stdio.h>
#include <unistd.h>
#include <string>
#include <iostream>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <microhttpd.h>
#include <sys/stat.h>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <pigpio.h>

// Include WiFi setup functionality
#include "wifi_setup.h"

// Pin definitions for Raspberry Pi (using pigpio numbering)
#define DOUT_PIN 5  // Direct BCM GPIO 5
#define CLK_PIN  6  // Direct BCM GPIO 6
#define PORT 8080   // Web server port

// Utility function to replace ends_with
bool ends_with(const std::string& str, const std::string& suffix) {
    if (str.length() < suffix.length()) return false;
    return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
}

// HX711 class implementation
class HX711 {
private:
    int DOUT, CLK;
    long OFFSET = 0;
    float SCALE = 1.0f;
    int GAIN = 128;
    
public:
    HX711(int dout, int clk) : DOUT(dout), CLK(clk) {
        gpioSetMode(CLK, PI_OUTPUT);
        gpioSetMode(DOUT, PI_INPUT);
        gpioWrite(CLK, 0);
    }
    
    ~HX711() {
        // Destructor code here (empty in this case)
    }

    bool is_ready() {
        return gpioRead(DOUT) == 0;
    }
    
    void set_gain(int gain = 128) {
        switch (gain) {
        case 128:
            GAIN = 1; break;
        case 64:
            GAIN = 3; break;
        case 32:
            GAIN = 2; break;
        default:
            GAIN = 1; break;
        }
    }
    
    long read() {
        // Wait until HX711 is ready
        while (!is_ready());
        
        unsigned long value = 0;
        
        // Pulse the clock pin 24 times to read data
        for (int i = 0; i < 24; i++) {
            gpioWrite(CLK, 1);
            gpioDelay(1);
            gpioWrite(CLK, 0);
            gpioDelay(1);
            
            value = value << 1;
            if (gpioRead(DOUT)) {
                value++;
            }
        }
        
        // Set the gain by pulsing the clock pin additional times
        for (int i = 0; i < GAIN; i++) {
            gpioWrite(CLK, 1);
            gpioDelay(1);
            gpioWrite(CLK, 0);
            gpioDelay(1);
        }
        
        // Convert 24-bit two's complement to signed 32-bit
        if (value & 0x800000) {
            value |= 0xFF000000;
        }
        
        return static_cast<long>(value);
    }
    
    long read_average(int times = 10) {
        long sum = 0;
        for (int i = 0; i < times; i++) {
            sum += read();
        }
        return sum / times;
    }
    
    void tare(int times = 10) {
        long sum = read_average(times);
        set_offset(sum);
    }
    
    void set_scale(float scale) {
        SCALE = scale;
    }
    
    void set_offset(long offset) {
        OFFSET = offset;
    }
    
    float get_units(int times = 10) {
        return (read_average(times) - OFFSET) / SCALE;
    }

    
};

// Global variables (PLACE THESE BEFORE ANY FUNCTIONS THAT USE THEM)
HX711* scale = nullptr;
float calibration_factor = -1100.0;
float inputWeight = 0.0;
float currentWeight = 0.0;
float netWeight = 0.0;
const float gramsToFluidOunces = 0.03527396;
std::mutex weightMutex;
std::atomic<bool> running{true};

// Forward declaration
static MHD_Result handle_request(void *cls, struct MHD_Connection *connection,
                          const char *url, const char *method,
                          const char *version, const char *upload_data,
                          unsigned int *upload_data_size, void **con_cols);

// Thread function for continuous measurement
void measurement_thread() {
    while (running) {
        float weight = scale->get_units(5);  // Average 5 readings
        
        {
            std::lock_guard<std::mutex> lock(weightMutex);
            currentWeight = weight;
            netWeight = weight - inputWeight;
        }
        
        // Delay between measurements
        usleep(500000);  // 500ms
    }
}

// Actual handle_request implementation
static MHD_Result handle_request(void *cls, struct MHD_Connection *connection,
                          const char *url, const char *method,
                          const char *version, const char *upload_data,
                          unsigned int *upload_data_size, void **con_cols) {
    
    static int request_counter = 0;
    struct MHD_Response *response;
    MHD_Result ret;
    
    // First call for this request - do nothing
    if (nullptr == *con_cols) {
        *con_cols = &request_counter;
        return MHD_YES;
    }
    
    // Process only GET requests
    if (0 != strcmp(method, "GET"))
        return MHD_NO;
    
    // Handle API requests
    if (0 == strcmp(url, "/api/measurements")) {
        // Create JSON with current weight measurements
        std::stringstream ss;
        
        // Lock for thread safety
        {
            std::lock_guard<std::mutex> lock(weightMutex);
            ss << "{";
            ss << "\"measured_weight_g\": " << currentWeight << ",";
            ss << "\"measured_weight_oz\": " << (currentWeight * gramsToFluidOunces) << ",";
            ss << "\"container_weight_g\": " << inputWeight << ",";
            ss << "\"container_weight_oz\": " << (inputWeight * gramsToFluidOunces) << ",";
            ss << "\"fluid_weight_g\": " << netWeight << ",";
            ss << "\"fluid_weight_oz\": " << (netWeight * gramsToFluidOunces);
            ss << "}";
        }
        
        // Create and send response
        std::string json = ss.str();
        response = MHD_create_response_from_buffer(json.length(), (void*)json.c_str(), MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "application/json");
        MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    } 
    // Rest of your existing request handling code...
    
    return MHD_YES;
}

int main() {
    
    // Check if Wi-Fi is configured
    if (!is_wifi_configured()) {
        std::cout << "No Wi-Fi configuration found. Starting setup mode..." << std::endl;
        start_ap_mode();
        return 0;
    }
    
    // Initialize pigpio
    if (gpioInitialise() == -1) {
        std::cerr << "Failed to initialize pigpio" << std::endl;
        return 1;
    }
    
    // Initialize the HX711
    scale = new HX711(DOUT_PIN, CLK_PIN);
    scale->set_scale(calibration_factor);
    scale->tare();
    
    std::cout << "HX711 Fluid Measurement System" << std::endl;
    std::cout << "Starting web server on port " << PORT << std::endl;
    
    // Create web server
    struct MHD_Daemon *daemon = MHD_start_daemon(
        MHD_USE_SELECT_INTERNALLY, PORT, NULL, NULL,
        reinterpret_cast<MHD_AccessHandlerCallback>(handle_request), NULL, 
        MHD_OPTION_END
    );
    
    if (daemon == NULL) {
        std::cerr << "Failed to start web server" << std::endl;
        delete scale;
        return 1;
    }
    
    // Start measurement thread
    std::thread meas_thread(measurement_thread);
    
    std::cout << "Server started. Press Enter to stop." << std::endl;
    getchar();
    
    // Cleanup
    running = false;
    meas_thread.join();
    MHD_stop_daemon(daemon);
    delete scale;
    
    gpioTerminate();

    return 0;
}

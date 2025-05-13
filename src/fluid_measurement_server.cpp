#include <stdio.h>
#include <wiringPi.h>
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

// Pin definitions for Raspberry Pi (using wiringPi numbering)
#define DOUT_PIN 9  // GPIO 5 (Pin 29) in BCM is wiringPi 9
#define CLK_PIN  6  // GPIO 6 (Pin 31) in BCM is wiringPi 6
#define PORT 8080   // Web server port

// HX711 class implementation
class HX711 {
private:
    int DOUT, CLK;
    long OFFSET = 0;
    float SCALE = 1.0f;
    int GAIN = 128;
    
public:
    HX711(int dout, int clk) : DOUT(dout), CLK(clk) {
        pinMode(CLK, OUTPUT);
        pinMode(DOUT, INPUT);
        digitalWrite(CLK, LOW);
    }
    
    bool is_ready() {
        return digitalRead(DOUT) == LOW;
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
            digitalWrite(CLK, HIGH);
            delayMicroseconds(1);
            digitalWrite(CLK, LOW);
            delayMicroseconds(1);
            
            value = value << 1;
            if (digitalRead(DOUT)) {
                value++;
            }
        }
        
        // Set the gain by pulsing the clock pin additional times
        for (int i = 0; i < GAIN; i++) {
            digitalWrite(CLK, HIGH);
            delayMicroseconds(1);
            digitalWrite(CLK, LOW);
            delayMicroseconds(1);
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

// Global variables
HX711* scale = nullptr;
float calibration_factor = -1100.0;
float inputWeight = 0.0;
float currentWeight = 0.0;
float netWeight = 0.0;
const float gramsToFluidOunces = 0.03527396;
std::mutex weightMutex;
std::atomic<bool> running{true};

// Function to handle web requests
static int handle_request(void *cls, struct MHD_Connection *connection,
                          const char *url, const char *method,
                          const char *version, const char *upload_data,
                          size_t *upload_data_size, void **con_cls) {
    
    static int request_counter = 0;
    struct MHD_Response *response;
    int ret;
    
    // First call for this request - do nothing
    if (nullptr == *con_cls) {
        *con_cls = &request_counter;
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
    } 
    // Handle API command endpoints
    else if (0 == strcmp(url, "/api/tare")) {
        scale->tare();
        response = MHD_create_response_from_buffer(15, (void*)"\"Scale tared\"", MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "application/json");
        MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    }
    else if (0 == strcmp(url, "/api/reset_container")) {
        inputWeight = 0.0;
        response = MHD_create_response_from_buffer(26, (void*)"\"Container weight reset\"", MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "application/json");
        MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    }
    // Serve static files (HTML, CSS, JS)
    else {
        std::string file_path = "web";
        
        // Default to index.html for root URL
        if (0 == strcmp(url, "/")) {
            file_path += "/index.html";
        } else {
            file_path += url;
        }
        
        // Read the file
        std::ifstream file(file_path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            // File not found, serve 404 page
            std::string not_found = "<html><body><h1>404 Not Found</h1></body></html>";
            response = MHD_create_response_from_buffer(not_found.length(), (void*)not_found.c_str(), MHD_RESPMEM_MUST_COPY);
            ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
            MHD_destroy_response(response);
            return ret;
        }
        
        // Get file size
        size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        // Read file content into buffer
        char* buffer = new char[file_size];
        file.read(buffer, file_size);
        file.close();
        
        // Create response
        response = MHD_create_response_from_buffer(file_size, buffer, MHD_RESPMEM_MUST_FREE);
        
        // Set appropriate content type based on file extension
        if (file_path.ends_with(".html")) {
            MHD_add_response_header(response, "Content-Type", "text/html");
        } else if (file_path.ends_with(".css")) {
            MHD_add_response_header(response, "Content-Type", "text/css");
        } else if (file_path.ends_with(".js")) {
            MHD_add_response_header(response, "Content-Type", "application/javascript");
        }
    }
    
    ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    
    return ret;
}

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

int main() {
    // Initialize wiringPi
    if (wiringPiSetup() == -1) {
        std::cerr << "Failed to initialize wiringPi" << std::endl;
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
        &handle_request, NULL, MHD_OPTION_END
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
    
    return 0;
}

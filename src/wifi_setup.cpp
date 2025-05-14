#include "wifi_setup.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <unistd.h>
#include <microhttpd.h>

#define SETUP_PORT 80

// Structure to hold form data
struct ConnectionInfo {
    std::string postData;
    bool processed;
};

// Check if Wi-Fi is configured
bool is_wifi_configured() {
    std::ifstream wpaConfig("/etc/wpa_supplicant/wpa_supplicant.conf");
    if (!wpaConfig.is_open()) {
        return false;
    }
    
    std::string line;
    while (std::getline(wpaConfig, line)) {
        if (line.find("ssid=") != std::string::npos) {
            return true;
        }
    }
    return false;
}

// POST request iterator callback
static int iterate_post(void *cls, enum MHD_ValueKind kind, 
                        const char *key, const char *filename, 
                        const char *content_type, const char *transfer_encoding, 
                        const char *data, uint64_t off, size_t size) {
    std::string *postData = static_cast<std::string*>(cls);
    if (key && data) {
        if (size > 0) {
            *postData += std::string(key) + "=" + std::string(data, size) + "&";
        }
    }
    return MHD_YES;
}

// Handle setup webpage requests
static int handle_setup_request(void *cls, struct MHD_Connection *connection,
                          const char *url, const char *method,
                          const char *version, const char *upload_data,
                          size_t *upload_data_size, void **con_cls) {
    
    if (!*con_cls) {
        // First call for this connection
        struct ConnectionInfo *info = new ConnectionInfo;
        info->processed = false;
        *con_cls = info;
        return MHD_YES;
    }
    
    struct ConnectionInfo *info = static_cast<ConnectionInfo*>(*con_cls);
    struct MHD_Response *response;
    int ret;
    
    // Handle POST request
    if (0 == strcmp(method, "POST")) {
        if (!info->processed) {
            if (*upload_data_size != 0) {
                // Process POST data
                if (0 == strcmp(url, "/setup")) {
                    MHD_post_process(static_cast<MHD_PostProcessor*>(*con_cls), upload_data, *upload_data_size);
                    info->postData.append(upload_data, *upload_data_size);
                }
                *upload_data_size = 0;
                return MHD_YES;
            }
            
            info->processed = true;
            
            // Parse POST data
            std::string ssid, password;
            size_t ssidPos = info->postData.find("ssid=");
            size_t pwdPos = info->postData.find("password=");
            
            if (ssidPos != std::string::npos && pwdPos != std::string::npos) {
                ssidPos += 5; // length of "ssid="
                size_t ssidEnd = info->postData.find("&", ssidPos);
                if (ssidEnd != std::string::npos) {
                    ssid = info->postData.substr(ssidPos, ssidEnd - ssidPos);
                }
                
                pwdPos += 9; // length of "password="
                size_t pwdEnd = info->postData.find("&", pwdPos);
                if (pwdEnd != std::string::npos) {
                    password = info->postData.substr(pwdPos, pwdEnd - pwdPos);
                } else {
                    password = info->postData.substr(pwdPos);
                }
            }
            
            if (!ssid.empty() && !password.empty()) {
                // Create wpa_supplicant.conf
                std::ofstream wpaConfig("/etc/wpa_supplicant/wpa_supplicant.conf");
                wpaConfig << "ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev\n";
                wpaConfig << "update_config=1\n";
                wpaConfig << "country=US\n\n"; // Change to appropriate country code
                wpaConfig << "network={\n";
                wpaConfig << "    ssid=\"" << ssid << "\"\n";
                wpaConfig << "    psk=\"" << password << "\"\n";
                wpaConfig << "    key_mgmt=WPA-PSK\n";
                wpaConfig << "}\n";
                wpaConfig.close();
                
                // Set permissions
                system("chmod 600 /etc/wpa_supplicant/wpa_supplicant.conf");
                
                // Return success page
                std::string success = "<html><head><meta http-equiv=\"refresh\" content=\"10;url=http://fluidscale.local:8080\"></head>"
                                    "<body><h1>Wi-Fi Setup Complete!</h1>"
                                    "<p>Your device will now connect to your Wi-Fi network.</p>"
                                    "<p>Please reconnect to your regular Wi-Fi network.</p>"
                                    "<p>The fluid scale will be available at: <a href=\"http://fluidscale.local:8080\">fluidscale.local:8080</a></p>"
                                    "</body></html>";
                
                response = MHD_create_response_from_buffer(success.length(), (void*)success.c_str(), MHD_RESPMEM_MUST_COPY);
                MHD_add_response_header(response, "Content-Type", "text/html");
                ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
                MHD_destroy_response(response);
                
                // Schedule a reboot to apply changes
                system("(sleep 5 && reboot) &");
                
                return ret;
            }
        }
    }
    
    // GET request - Serve setup page
    if (0 == strcmp(method, "GET") && (0 == strcmp(url, "/") || 0 == strcmp(url, "/setup"))) {
        // Scan for Wi-Fi networks
        system("iwlist wlan0 scan | grep ESSID | awk -F':' '{print $2}' | sed 's/\"//g' > /tmp/wifi_networks");
        std::ifstream wifiScan("/tmp/wifi_networks");
        std::string line;
        std::vector<std::string> networks;
        
        while (std::getline(wifiScan, line)) {
            if (!line.empty()) {
                networks.push_back(line);
            }
        }
        
        // Create setup page HTML
        std::string setup_page = "<html><head><title>Fluid Scale Wi-Fi Setup</title>"
                              "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                              "<style>"
                              "body { font-family: 'SF Pro Display', -apple-system, sans-serif; margin: 0; padding: 20px; }"
                              "h1 { color: #1d1d1f; text-align: center; margin-bottom: 30px; }"
                              "form { max-width: 400px; margin: 0 auto; background: #fff; padding: 20px; border-radius: 12px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }"
                              "label { display: block; margin-bottom: 5px; font-weight: 500; }"
                              "select, input { width: 100%; padding: 12px; margin-bottom: 20px; border: 1px solid #d2d2d7; border-radius: 8px; font-size: 16px; }"
                              "button { background: #0071e3; color: white; border: none; padding: 12px 0; width: 100%; font-size: 16px; border-radius: 8px; cursor: pointer; }"
                              "button:hover { background: #0062c3; }"
                              "</style></head>"
                              "<body>"
                              "<h1>Fluid Scale<br>Wi-Fi Setup</h1>"
                              "<form action=\"/setup\" method=\"post\">"
                              "<label for=\"ssid\">Select Wi-Fi Network:</label>"
                              "<select name=\"ssid\" id=\"ssid\">";
        
        // Add network options
        for (const auto& network : networks) {
            setup_page += "<option value=\"" + network + "\">" + network + "</option>";
        }
        
        // Add manual entry option
        setup_page += "<option value=\"manual\">Enter manually...</option>"
                      "</select>"
                      "<div id=\"manual-ssid\" style=\"display:none;\">"
                      "<label for=\"manual-ssid-input\">Network Name (SSID):</label>"
                      "<input type=\"text\" id=\"manual-ssid-input\" name=\"manual-ssid\">"
                      "</div>"
                      "<label for=\"password\">Wi-Fi Password:</label>"
                      "<input type=\"password\" name=\"password\" id=\"password\">"
                      "<button type=\"submit\">Connect</button>"
                      "</form>"
                      "<script>"
                      "document.getElementById('ssid').addEventListener('change', function() {"
                      "  var manualDiv = document.getElementById('manual-ssid');"
                      "  if (this.value === 'manual') {"
                      "    manualDiv.style.display = 'block';"
                      "  } else {"
                      "    manualDiv.style.display = 'none';"
                      "  }"
                      "});"
                      "</script>"
                      "</body></html>";
        
        response = MHD_create_response_from_buffer(setup_page.length(), (void*)setup_page.c_str(), MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "text/html");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        
        return ret;
    }
    
    // Default 404 response
    std::string not_found = "<html><body><h1>404 Not Found</h1></body></html>";
    response = MHD_create_response_from_buffer(not_found.length(), (void*)not_found.c_str(), MHD_RESPMEM_MUST_COPY);
    ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
    MHD_destroy_response(response);
    
    return ret;
}

// Request completed callback
static void request_completed(void *cls, struct MHD_Connection *connection,
                             void **con_cls, enum MHD_RequestTerminationCode toe) {
    if (*con_cls) {
        struct ConnectionInfo *info = static_cast<ConnectionInfo*>(*con_cls);
        delete info;
        *con_cls = NULL;
    }
}

// Start access point mode for WiFi setup
void start_ap_mode() {
    std::cout << "Setting up access point mode for WiFi configuration..." << std::endl;
    
    // Create hostapd configuration
    std::ofstream hostapd("/etc/hostapd/hostapd.conf");
    hostapd << "interface=wlan0\n";
    hostapd << "driver=nl80211\n";
    hostapd << "ssid=FluidScale-Setup\n";
    hostapd << "hw_mode=g\n";
    hostapd << "channel=7\n";
    hostapd << "wmm_enabled=0\n";
    hostapd << "macaddr_acl=0\n";
    hostapd << "auth_algs=1\n";
    hostapd << "ignore_broadcast_ssid=0\n";
    hostapd << "wpa=2\n";
    hostapd << "wpa_passphrase=fluidscale\n";
    hostapd << "wpa_key_mgmt=WPA-PSK\n";
    hostapd << "wpa_pairwise=TKIP\n";
    hostapd << "rsn_pairwise=CCMP\n";
    hostapd.close();
    
    // Create dnsmasq configuration
    std::ofstream dnsmasq("/etc/dnsmasq.conf");
    dnsmasq << "interface=wlan0\n";
    dnsmasq << "dhcp-range=192.168.4.2,192.168.4.20,255.255.255.0,24h\n";
    dnsmasq << "address=/#/192.168.4.1\n";
    dnsmasq.close();
    
    // Stop network services
    system("systemctl stop dhcpcd");
    system("systemctl stop wpa_supplicant");
    
    // Configure as access point
    system("ifconfig wlan0 down");
    system("ifconfig wlan0 192.168.4.1 up");
    system("systemctl start dnsmasq");
    system("hostapd -B /etc/hostapd/hostapd.conf");
    
    std::cout << "Access point started. SSID: FluidScale-Setup, Password: fluidscale" << std::endl;
    std::cout << "Starting web server for setup..." << std::endl;
    
    // Start web server for setup
    struct MHD_Daemon *daemon = MHD_start_daemon(
        MHD_USE_SELECT_INTERNALLY, SETUP_PORT, NULL, NULL,
        &handle_setup_request, NULL, 
        MHD_OPTION_NOTIFY_COMPLETED, request_completed, NULL,
        MHD_OPTION_END
    );
    
    if (daemon == NULL) {
        std::cerr << "Failed to start setup web server" << std::endl;
        return;
    }
    
    std::cout << "Setup web server started on port 80" << std::endl;
    std::cout << "Connect to 'FluidScale-Setup' network to configure Wi-Fi" << std::endl;
    
    // Keep running until configuration is complete
    while (!is_wifi_configured()) {
        sleep(5);
    }
    
    std::cout << "Wi-Fi configuration complete. Stopping setup mode..." << std::endl;
    
    MHD_stop_daemon(daemon);
    
    // Stop AP services
    system("systemctl stop hostapd");
    system("systemctl stop dnsmasq");
    
    // Reset network interface
    system("ifconfig wlan0 down");
    system("ifconfig wlan0 up");
    
    // Start normal network services
    system("systemctl start dhcpcd");
    system("systemctl start wpa_supplicant");
    
    std::cout << "Setup mode complete. Restarting to apply Wi-Fi configuration..." << std::endl;
}

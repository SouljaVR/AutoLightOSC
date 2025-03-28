// Copyright (c) 2025 BigSoulja/SouljaVR
// Developed and maintained by BigSoulja/SouljaVR and all direct or indirect contributors to the GitHub repository.
// See LICENSE.txt for full copyright and licensing details (GNU General Public License v3.0).
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// This project is open source, but continued development and maintenance benefit from your support.
// Businesses and collaborators: support via funding, sponsoring, or integration opportunities is welcome.
// For inquiries or support, please reach out at: Discord: @bigsoulja

// OscManager.cpp

#include "OscManager.h"
#include <iostream>
#include <iomanip>
#include <sstream>

#define OSC_BUFFER_SIZE 1024

OscManager::OscManager(const std::string& ipAddress, int port)
    : ipAddress(ipAddress), port(port), oscRate(0),
    rParameter("AL_Red"), gParameter("AL_Green"), bParameter("AL_Blue"),
    lastMessageTime(std::chrono::steady_clock::now()) {
    Initialize();
}

OscManager::~OscManager() {
    // Socket will be automatically cleaned up by unique_ptr
}

void OscManager::Initialize() {
    try {
        socket = std::make_unique<UdpTransmitSocket>(
            IpEndpointName(ipAddress.c_str(), port)
        );
    }
    catch (const std::exception& e) {
        std::cerr << "Error initializing OSC sender: " << e.what() << std::endl;
        socket.reset();
    }
}

void OscManager::SetOscRate(int rate) {
    oscRate = rate;
}

void OscManager::SetOscPort(int newPort) {
    if (port != newPort) {
        port = newPort;
        // Reinitialize with new port
        socket.reset();
        Initialize();
    }
}

void OscManager::SetParameters(const std::string& r, const std::string& g, const std::string& b) {
    rParameter = r;
    gParameter = g;
    bParameter = b;
}

void OscManager::SendColorValues(float r, float g, float b) {
    if (!socket) {
        Initialize();
        if (!socket) return;
    }

    try {
        // Map values from [0,1] to [-1,1] for OSC
        float rMapped = r * 2.0f - 1.0f;
        float gMapped = g * 2.0f - 1.0f;
        float bMapped = b * 2.0f - 1.0f;

        // Set precision to 3 decimal places
        auto capToThreeDecimals = [](float value) {
            std::ostringstream out;
            out << std::fixed << std::setprecision(3) << value;
            return std::stof(out.str());
        };

        rMapped = capToThreeDecimals(rMapped);
        gMapped = capToThreeDecimals(gMapped);
        bMapped = capToThreeDecimals(bMapped);

        // Create and send OSC messages
        char buffer[OSC_BUFFER_SIZE];
        osc::OutboundPacketStream p(buffer, OSC_BUFFER_SIZE);

        // Use the configurable parameter names
        std::string rPath = "/avatar/parameters/" + rParameter;
        std::string gPath = "/avatar/parameters/" + gParameter;
        std::string bPath = "/avatar/parameters/" + bParameter;

        p << osc::BeginMessage(rPath.c_str()) << rMapped << osc::EndMessage;
        socket->Send(p.Data(), p.Size());
        p.Clear();

        p << osc::BeginMessage(gPath.c_str()) << gMapped << osc::EndMessage;
        socket->Send(p.Data(), p.Size());
        p.Clear();

        p << osc::BeginMessage(bPath.c_str()) << bMapped << osc::EndMessage;
        socket->Send(p.Data(), p.Size());
    }
    catch (const std::exception& e) {
        std::cerr << "Error sending OSC message: " << e.what() << std::endl;
        socket.reset(); // Force reinitialization on next attempt
    }
}
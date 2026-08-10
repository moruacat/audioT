// modem_factory.cpp - Factory implementation for creating modem instances
#include "modem_factory.h"
#include "afsk_modem.h"
#include "psk_modem.h"
#include "qam_modem.h"

namespace audiot {

std::unique_ptr<IModem> ModemFactory::create(ModemType type, int sampleRate) {
    switch (type) {
        case ModemType::AFSK: {
            AFSKModem::Config config;
            config.sampleRate = sampleRate;
            return std::unique_ptr<IModem>(new AFSKModem(config));
        }
        case ModemType::PSK: {
            PSKModem::Config config;
            config.sampleRate = sampleRate;
            return std::unique_ptr<IModem>(new PSKModem(config));
        }
        case ModemType::QAM: {
            QAMModem::Config config;
            config.sampleRate = sampleRate;
            return std::unique_ptr<IModem>(new QAMModem(config));
        }
        default:
            return nullptr;
    }
}

std::vector<ModemType> ModemFactory::supportedTypes() {
    std::vector<ModemType> types;
    types.push_back(ModemType::AFSK);
    types.push_back(ModemType::PSK);
    types.push_back(ModemType::QAM);
    return types;
}

std::string ModemFactory::typeName(ModemType type) {
    switch (type) {
        case ModemType::AFSK:
            return "AFSK";
        case ModemType::PSK:
            return "PSK";
        case ModemType::QAM:
            return "QAM";
        default:
            return "Unknown";
    }
}

std::string ModemFactory::typeDescription(ModemType type) {
    switch (type) {
        case ModemType::AFSK:
            return "Audio Frequency Shift Keying (Bell 202 style, 1200/2200 Hz)";
        case ModemType::PSK:
            return "Phase Shift Keying (BPSK/QPSK/8PSK with Costas loop)";
        case ModemType::QAM:
            return "Quadrature Amplitude Modulation (16/32/64-QAM)";
        default:
            return "Unknown modulation type";
    }
}

} // namespace audiot

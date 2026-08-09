// modem_factory.h - Factory for creating modem instances
#ifndef AUDIOT_MODEM_FACTORY_H
#define AUDIOT_MODEM_FACTORY_H

#include <memory>
#include <vector>
#include <string>
#include "interfaces/IModem.h"

namespace audiot {

// Factory class for creating modem instances based on modulation type
class ModemFactory {
public:
    // Create a modem instance based on type
    static std::unique_ptr<IModem> create(ModemType type, int sampleRate = 44100);

    // Get list of supported modem types
    static std::vector<ModemType> supportedTypes();

    // Get human-readable name for modem type
    static std::string typeName(ModemType type);

    // Get description of modem type
    static std::string typeDescription(ModemType type);
};

} // namespace audiot
#endif // AUDIOT_MODEM_FACTORY_H

#ifndef COMMAND_TRANSLATOR_H
#define COMMAND_TRANSLATOR_H

#include <ArduinoJson.h>

// Translates legacy {"command":"motor_control"/"servo_control"} JSON in-place
// to the new {"cmd":"pwm"/"servo"} format aligned with motorControl.
// Call once at the top of CommandProcessor::processCommands() before dispatch.
class CommandTranslator {
public:
    static void translate(JsonDocument &doc) {
        if (!doc.containsKey("command")) return;

        const char *command = doc["command"] | "";

        if (strcmp(command, "motor_control") == 0) {
            int motor = doc["motor"].as<int>();
            const char *dirStr = doc["direction"] | "R";
            int speed = doc["speed"] | 0;
            char dir = dirStr[0];

            doc.remove("command");
            doc.remove("direction");
            doc.remove("speed");
            doc["cmd"] = "pwm";
            doc["motor"] = motor;

            if (dir == 'F' || dir == 'f') {
                doc["duty"] = speed * 100 / 255;
            } else if (dir == 'B' || dir == 'b') {
                doc["duty"] = -(speed * 100 / 255);
            } else {
                // RELEASE or STOP -> convert to stop command
                doc["cmd"] = "stop";
            }
        } else if (strcmp(command, "servo_control") == 0) {
            int servo = doc["servo"].as<int>();
            int angle = doc["angle"] | 90;

            doc.remove("command");
            doc.remove("servo");
            doc.remove("angle");
            doc["cmd"] = "servo";
            doc["ch"] = servo;
            doc["deg"] = angle;
        }
        // Other legacy commands pass through untranslated
    }
};

#endif

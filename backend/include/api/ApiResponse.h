#ifndef PAYLOCK_API_RESPONSE_H
#define PAYLOCK_API_RESPONSE_H

#include <string>
#include <nlohmann/json.hpp>
#include <httplib.h>

namespace paylock {
namespace api {

/**
 * @brief Helper functions to generate standardized PayLock JSON responses.
 */
class ApiResponse {
public:
    static void sendSuccess(httplib::Response& res,
                            const nlohmann::json& data,
                            const std::string& message = "Request completed successfully",
                            int statusCode = 200) {
        nlohmann::json body;
        body["success"] = true;
        body["data"] = data;
        body["message"] = message;

        res.status = statusCode;
        res.set_content(body.dump(2), "application/json");
    }

    static void sendError(httplib::Response& res,
                          const std::string& code,
                          const std::string& message,
                          int statusCode = 400) {
        nlohmann::json body;
        body["success"] = false;
        body["error"] = {
            {"code", code},
            {"message", message}
        };

        res.status = statusCode;
        res.set_content(body.dump(2), "application/json");
    }
};

} // namespace api
} // namespace paylock

#endif // PAYLOCK_API_RESPONSE_H

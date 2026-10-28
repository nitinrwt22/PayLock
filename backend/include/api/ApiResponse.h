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
    static std::string buildSuccessJson(const nlohmann::json& data,
                                        const std::string& message = "Request completed successfully") {
        nlohmann::json body;
        body["success"] = true;
        body["data"] = data;
        body["message"] = message;
        return body.dump(2);
    }

    static std::string buildErrorJson(const std::string& code,
                                      const std::string& message) {
        nlohmann::json body;
        body["success"] = false;
        body["error"] = {
            {"code", code},
            {"message", message}
        };
        return body.dump(2);
    }

    static void sendSuccess(httplib::Response& res,
                            const nlohmann::json& data,
                            const std::string& message = "Request completed successfully",
                            int statusCode = 200) {
        res.status = statusCode;
        res.set_content(buildSuccessJson(data, message), "application/json");
    }

    static void sendError(httplib::Response& res,
                          const std::string& code,
                          const std::string& message,
                          int statusCode = 400) {
        res.status = statusCode;
        res.set_content(buildErrorJson(code, message), "application/json");
    }
};

} // namespace api
} // namespace paylock

#endif // PAYLOCK_API_RESPONSE_H

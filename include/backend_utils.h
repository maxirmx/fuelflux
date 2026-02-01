// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace fuelflux {

// Поскольку контроллер обладает крайне ограниченными возможностями по выводу текстовых сообщений,
// и наличие квалифициированного персонала рядом не предполагается, мы ограничимся двумя универсальными
// сообщениями об ошибке.
inline const std::string StdControllerError = "Ошибка контроллера";
inline const std::string StdBackendError = "Ошибка портала";
constexpr int HttpRequestWrapperErrorCode = -1;
inline const std::string HttpRequestWrapperErrorText = "Ошибка связи с сервером";

inline bool IsErrorResponse(const nlohmann::json& response, std::string* errorText) {
    if (!response.is_object()) {
        return false;
    }
    const auto codeIt = response.find("CodeError");
    if (codeIt == response.end() || !codeIt->is_number_integer()) {
        return false;
    }
    const int code = codeIt->get<int>();
    if (code == 0) {
        return false;
    }
    if (errorText) {
        *errorText = response.value("TextError", "Неизвестная ошибка");
    }
    return true;
}

inline nlohmann::json BuildWrapperErrorResponse() {
    return nlohmann::json{
        {"CodeError", HttpRequestWrapperErrorCode},
        {"TextError", HttpRequestWrapperErrorText}
    };
}

} // namespace fuelflux

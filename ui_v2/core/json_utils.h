#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QString>
#include <nlohmann/json.hpp>

namespace llm_re::ui_v2 {

using json = nlohmann::json;

// Utility class for efficient JSON conversions between nlohmann::json and Qt JSON
class JsonUtils {
public:
    // Convert nlohmann::json to QJsonObject - more efficient than string roundtrip
    static QJsonObject jsonToQJson(const json& j) {
        if (!j.is_object()) {
            return QJsonObject();
        }
        
        QJsonObject obj;
        for (auto it = j.begin(); it != j.end(); ++it) {
            obj[QString::fromStdString(it.key())] = jsonValueToQJson(it.value());
        }
        return obj;
    }
    
    // Convert nlohmann::json to QJsonArray
    static QJsonArray jsonArrayToQJson(const json& j) {
        if (!j.is_array()) {
            return QJsonArray();
        }
        
        QJsonArray arr;
        for (const auto& item : j) {
            arr.append(jsonValueToQJson(item));
        }
        return arr;
    }
    
    // Convert QJsonObject to nlohmann::json
    static json qJsonToJson(const QJsonObject& obj) {
        json j = json::object();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            j[it.key().toStdString()] = qJsonValueToJson(it.value());
        }
        return j;
    }
    
    // Convert QJsonArray to nlohmann::json
    static json qJsonArrayToJson(const QJsonArray& arr) {
        json j = json::array();
        for (const auto& val : arr) {
            j.push_back(qJsonValueToJson(val));
        }
        return j;
    }
    
private:
    // Helper to convert individual JSON values
    static QJsonValue jsonValueToQJson(const json& j) {
        if (j.is_null()) {
            return QJsonValue();
        } else if (j.is_boolean()) {
            return QJsonValue(j.get<bool>());
        } else if (j.is_number_integer()) {
            return QJsonValue(static_cast<int>(j.get<int64_t>()));
        } else if (j.is_number_float()) {
            return QJsonValue(j.get<double>());
        } else if (j.is_string()) {
            return QJsonValue(QString::fromStdString(j.get<std::string>()));
        } else if (j.is_array()) {
            return jsonArrayToQJson(j);
        } else if (j.is_object()) {
            return jsonToQJson(j);
        }
        return QJsonValue();
    }
    
    // Helper to convert QJsonValue to nlohmann::json
    static json qJsonValueToJson(const QJsonValue& val) {
        switch (val.type()) {
            case QJsonValue::Null:
            case QJsonValue::Undefined:
                return nullptr;
            case QJsonValue::Bool:
                return val.toBool();
            case QJsonValue::Double:
                return val.toDouble();
            case QJsonValue::String:
                return val.toString().toStdString();
            case QJsonValue::Array:
                return qJsonArrayToJson(val.toArray());
            case QJsonValue::Object:
                return qJsonToJson(val.toObject());
        }
        return nullptr;
    }
};

} // namespace llm_re::ui_v2
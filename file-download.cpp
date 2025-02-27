#include <iostream>
#include <fstream>
#include <sstream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <jwt-cpp/jwt.h>

using json = nlohmann::json;

std::string read_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filename);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string get_access_token(const std::string& service_account_file) {
    std::string key_content = read_file(service_account_file);
    auto key_json = json::parse(key_content);

    std::string private_key = key_json["private_key"];
    std::string client_email = key_json["client_email"];

    auto now = std::chrono::system_clock::now();
    auto expiration = now + std::chrono::minutes(60);

    std::string jwt_token = jwt::create()
        .set_issuer(client_email)
        .set_subject(client_email)
        .set_audience("https://oauth2.googleapis.com/token")
        .set_issued_at(now)
        .set_expires_at(expiration)
        .set_type("JWT")
        .sign(jwt::algorithm::rs256("", private_key, "", ""));

    std::string post_data = "grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer&assertion=" + jwt_token;

    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize cURL.");
    }

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, "https://oauth2.googleapis.com/token");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, [](char* ptr, size_t size, size_t nmemb, std::string* data) -> size_t {
        data->append(ptr, size * nmemb);
        return size * nmemb;
    });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error("Failed to get access token.");
    }

    auto response_json = json::parse(response);
    return response_json["access_token"];
}

size_t write_data(void* ptr, size_t size, size_t nmemb, std::ofstream* file) {
    file->write((char*)ptr, size * nmemb);
    return size * nmemb;
}

void download_file(const std::string& bucket_name, const std::string& object_name, const std::string& destination, const std::string& access_token) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize cURL.");
    }

    std::string url = "https://storage.googleapis.com/storage/v1/b/" + bucket_name + "/o/" + curl_easy_escape(curl, object_name.c_str(), 0) + "?alt=media";
    std::ofstream output_file(destination, std::ios::binary);

    struct curl_slist* headers = NULL;
    std::string auth_header = "Authorization: Bearer " + access_token;
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output_file);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    output_file.close();

    if (res != CURLE_OK) {
        throw std::runtime_error("Failed to download file.");
    }
}

int main() {
    try {
        std::string service_account_file = "service-account.json"; // Replace with your service account JSON file
        std::string bucket_name = "your-bucket-name"; // Replace with your bucket name
        std::string object_name = "your-object-name"; // Replace with your object name
        std::string destination = "downloaded_file.txt"; // Replace with the local file path

        std::string access_token = get_access_token(service_account_file);
        download_file(bucket_name, object_name, destination, access_token);

        std::cout << "File downloaded successfully!" << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}

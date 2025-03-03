#include <iostream>
#include <fstream>
#include <sstream>
#include <curl/curl.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <ctime>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Function to read a file into a string
std::string read_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filename);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Function to Base64 URL encode a string
std::string base64_url_encode(const std::string &input) {
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;
    
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, input.c_str(), input.length());
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);
    
    std::string output(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);

    // Replace URL-unsafe characters
    for (char &c : output) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    
    return output;
}

// Function to sign JWT using OpenSSL
std::string sign_jwt(const std::string &header, const std::string &payload, const std::string &private_key) {
    std::string jwt_data = base64_url_encode(header) + "." + base64_url_encode(payload);

    BIO *bio = BIO_new_mem_buf(private_key.c_str(), -1);
    RSA *rsa = PEM_read_bio_RSAPrivateKey(bio, NULL, NULL, NULL);
    BIO_free(bio);

    if (!rsa) {
        throw std::runtime_error("Failed to load RSA private key.");
    }

    unsigned char signature[256];
    unsigned int signature_length;
    
    EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
    EVP_SignInit(md_ctx, EVP_sha256());
    EVP_SignUpdate(md_ctx, jwt_data.c_str(), jwt_data.length());
    EVP_SignFinal(md_ctx, signature, &signature_length, EVP_PKEY_new_mac_key(EVP_PKEY_RSA, NULL, (unsigned char *)RSA_get0_n(rsa), RSA_size(rsa)));
    EVP_MD_CTX_free(md_ctx);
    RSA_free(rsa);

    std::string signed_token = jwt_data + "." + base64_url_encode(std::string((char*)signature, signature_length));
    return signed_token;
}

// Function to obtain an OAuth 2.0 access token
std::string get_access_token(const std::string& service_account_file) {
    std::string key_content = read_file(service_account_file);
    auto key_json = json::parse(key_content);

    std::string private_key = key_json["private_key"];
    std::string client_email = key_json["client_email"];

    time_t now = time(NULL);
    time_t exp = now + 3600; // 1 hour expiry

    // Create JWT header and payload
    json header = { {"alg", "RS256"}, {"typ", "JWT"} };
    json payload = {
        {"iss", client_email},
        {"sub", client_email},
        {"aud", "https://oauth2.googleapis.com/token"},
        {"iat", now},
        {"exp", exp}
    };

    // Convert to string
    std::string header_str = header.dump();
    std::string payload_str = payload.dump();

    // Sign JWT
    std::string jwt = sign_jwt(header_str, payload_str, private_key);

    // Make POST request to Google OAuth2 API
    std::string post_data = "grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer&assertion=" + jwt;
    
    CURL* curl = curl_easy_init();
    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, "https://oauth2.googleapis.com/token");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, [](char* ptr, size_t size, size_t nmemb, std::string* data) -> size_t {
        data->append(ptr, size * nmemb);
        return size * nmemb;
    });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    auto response_json = json::parse(response);
    return response_json["access_token"];
}

// Function to download a file from Google Cloud Storage
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
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, [](char* ptr, size_t size, size_t nmemb, std::ofstream* file) -> size_t {
        file->write(ptr, size * nmemb);
        return size * nmemb;
    });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output_file);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    output_file.close();
}

int main() {
    try {
        std::string service_account_file = R"(\\hbsk\gt\app\sa.json)"; // Your service account path
        std::string bucket_name = "your-bucket-name";
        std::string object_name = "your-object-name";
        std::string destination = "downloaded_file.txt";

        std::string access_token = get_access_token(service_account_file);
        download_file(bucket_name, object_name, destination, access_token);

        std::cout << "File downloaded successfully!" << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}

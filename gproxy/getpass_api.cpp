#include "Include.h"
#include "gproxy_imgui.h"
#include "srv_Url.h"

// ============================================================================
// Struct kết quả
// ============================================================================
struct GetPassResult 
{
    bool        success;
    std::string message;
    std::string password;   // password thật trả về từ API
    int         code;
};

// ============================================================================
// Global state cho thread (giống pattern RegisterThread)
// ============================================================================
static bool        g_GetPassInProgress    = false;
static bool        g_GetPassResultReady   = false;
static bool        g_GetPassSuccess       = false;
static std::string g_GetPassResultMessage;
static std::string g_GetPassResultPassword;

// ============================================================================
// curl write callback
// ============================================================================
static size_t GetPassWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// ============================================================================
// CallGetPassAPI - gọi API lấy pass
// ============================================================================
GetPassResult CallGetPassAPI(const std::string& username, const std::string& email)
{
    GetPassResult result = { false, "", "", 0 };

    CURL* curl = curl_easy_init();
    if (!curl) 
    {
        result.message = "Failed to initialize CURL";
        return result;
    }

    // Build POST data (URL encoded form)
    std::string postData = "username=" + username + "&email=" + email;

    // Response buffer
    std::string response;

    // --- Set CURL options ---
    curl_easy_setopt(curl, CURLOPT_URL,         ServerConfig::GETPASS_URL.c_str());
    curl_easy_setopt(curl, CURLOPT_POST,        1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,  postData.c_str());

    // Headers
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "User-Agent: GProxy/1.0");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // Write callback
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, GetPassWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &response);

    // Timeout
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        15L);   // API crack có thể chậm hơn
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    // --- Perform request ---
    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) 
    {
        result.message = "Request failed: " + std::string(curl_easy_strerror(res));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return result;
    }

    // Get HTTP status code
    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    result.code = (int)statusCode;

    // Cleanup curl
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    // --- Parse JSON response bằng nlohmann/json ---
    try 
    {
        json j = json::parse(response);

        result.success = j.value("success", false);
        result.message = j.value("message", "");

        // Nếu success, lấy password từ data.password
        if (result.success && j.contains("data")) 
        {
            result.password = j["data"].value("password", "");
        }
    }
    catch (const json::parse_error&) 
    {
        // Nếu parse thất bại -> response không phải JSON hợp lệ
        result.success = false;
        result.message = g_SelecteNgonNgu ? "Failed to parse API response" : "Không thể phân tích cú pháp phản hồi API";
    }

    // Fallback message nếu API không trả message
    if (result.message.empty()) 
    {
        if (result.success) 
        {
            result.message = g_SelecteNgonNgu ? "Password retrieved successfully!" : "Lấy mật khẩu thành công!";
        }
        else if (result.code == 404) 
        {
            result.message = g_SelecteNgonNgu ? "Account not found or email does not match!" : "Không tìm thấy tài khoản hoặc email không khớp!";
        }
        else if (result.code == 400) 
        {
            result.message = g_SelecteNgonNgu ? "Invalid input!" : "Dữ liệu nhập không hợp lệ!";
        }
        else if (result.code == 502 || result.code == 503) 
        {
            result.message = g_SelecteNgonNgu ? "Crack API is unavailable, try again later!" : "Crack API không khả dụng, hãy thử lại sau!";
        }
        else 
        {
            result.message = "Failed (HTTP " + std::to_string(result.code) + ")";
        }
    }

    return result;
}

// ============================================================================
// Thread wrapper - gọi từ UI thread
// ============================================================================
void GetPassThreadFunc(std::string username, std::string email)
{
    GetPassResult result = CallGetPassAPI(username, email);

    g_GetPassSuccess        = result.success;
    g_GetPassResultMessage  = result.message;
    g_GetPassResultPassword = result.password;
    g_GetPassResultReady    = true;
    g_GetPassInProgress     = false;
}

bool               showGetPassPopup = false;
static char        gpEmailBuffer[256] = "";
static char        usernameBuffer[256] = "";
static char        passwordBuffer[256] = "";
static std::string g_GetPassPopupMessage = "";   // message hiển thị trong popup
static int         g_GetPassPopupMsgType = 0;    // 0=none, 1=success, 2=error, 3=loading

void RenderGetPassPopup()
{
    // --- Mở popup khi flag được set ---
    if (showGetPassPopup) 
    {
        ImGui::OpenPopup(g_SelecteNgonNgu ? "Forgot password" : "Quên mật khẩu");
        showGetPassPopup = false;  // reset flag sau khi open
    }

    // --- Setup window ---
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420, 320));

    bool isOpen = true;
    if (!ImGui::BeginPopupModal(g_SelecteNgonNgu ? "Forgot password" : "Quên mật khẩu", &isOpen, ImGuiWindowFlags_NoResize))
        return;

    // --- Header: server info ---
    ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), g_SelecteNgonNgu ? "Server: %s" : "Máy chủ: %s", serverBuffer);
    ImGui::Separator();
    ImGui::Spacing();

    // --- Disable input khi đang gửi request ---
    bool busy = g_GetPassInProgress;
    if (busy) ImGui::BeginDisabled();

    // Username
    ImGui::Text(g_SelecteNgonNgu ? "Acccount:" : "Tài khoản:");
    ImGui::SameLine(100);
    ImGui::PushItemWidth(-10);
    ImGui::InputText("##GPUsername", usernameBuffer, sizeof(usernameBuffer));
    ImGui::PopItemWidth();

    // Email
    ImGui::Text("Email:");
    ImGui::SameLine(100);
    ImGui::PushItemWidth(-10);
    ImGui::InputText("##GPEmail", gpEmailBuffer, sizeof(gpEmailBuffer));
    ImGui::PopItemWidth();

    if (busy) ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::Text(g_SelecteNgonNgu ? "Password:" : "Mật khẩu:");
    ImGui::SameLine(100);
    ImGui::PushItemWidth(-10);
    ImGui::InputText("##output", passwordBuffer, sizeof(passwordBuffer), ImGuiInputTextFlags_ReadOnly);
    ImGui::PopItemWidth();

    ImGui::Spacing();
    ImGui::Separator();

    // --- Vùng hiển thị kết quả (cố định chiều cao để buttons không dịch chuyển) ---
    ImGui::Spacing();
    ImGui::BeginChild("##GetPassMsgArea", ImVec2(0, 60), false);
    {
        if (g_GetPassPopupMsgType == 1) 
        {
            // Success - xanh
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
            ImGui::TextWrapped(u8"✓ %s", g_GetPassPopupMessage.c_str());
            ImGui::PopStyleColor();
        }
        else if (g_GetPassPopupMsgType == 2) 
        {
            // Error - đỏ
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            ImGui::TextWrapped(u8"✗ %s", g_GetPassPopupMessage.c_str());
            ImGui::PopStyleColor();
        }
        else if (g_GetPassPopupMsgType == 3) 
        {
            // Loading - vàng
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.3f, 1.0f));
            ImGui::TextWrapped(u8"⏳ %s", g_GetPassPopupMessage.c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();

    // --- Check thread result mỗi frame ---
    if (g_GetPassResultReady) 
    {
        g_GetPassResultReady = false;

        if (g_GetPassSuccess) 
        {
            g_GetPassPopupMsgType = 1;  // success
            g_GetPassPopupMessage = g_SelecteNgonNgu ? "Password: " : "Mật khẩu: " + g_GetPassResultPassword;

            // Auto-fill password buffer cho login form
            strncpy(passwordBuffer, g_GetPassResultPassword.c_str(), sizeof(passwordBuffer) - 1);
            passwordBuffer[sizeof(passwordBuffer) - 1] = '\0';
        }
        else 
        {
            g_GetPassPopupMsgType = 2;  // error
            g_GetPassPopupMessage = g_GetPassResultMessage;
        }
    }

    // --- Buttons căn giữa ---
    ImGui::Spacing();
    {
        float btnW = 120.0f;
        float btnGap = 20.0f;
        float totalW = btnW * 2 + btnGap;
        float startX = (ImGui::GetWindowWidth() - totalW) * 0.5f;
        ImGui::SetCursorPosX(startX);

        // Button "Lấy Pass"
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.9f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.6f, 0.8f, 1.0f));

        bool clickedGet = false;
        if (busy) 
        {
            ImGui::BeginDisabled();
            ImGui::Button(g_SelecteNgonNgu ? "Get Pass" : "Lấy Pass", ImVec2(btnW, 30));
            ImGui::EndDisabled();
        }
        else 
        {
            clickedGet = ImGui::Button(g_SelecteNgonNgu ? "Get Pass" : "Lấy Pass", ImVec2(btnW, 30));
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0, btnGap);

        // Button "Đóng"
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        if (ImGui::Button(g_SelecteNgonNgu ? "Close" : "Đóng", ImVec2(btnW, 30)))
        {
            g_GetPassPopupMessage = "";
            g_GetPassPopupMsgType = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);

        // --- Handle click "Lấy Pass" ---
        if (clickedGet) 
        {
            std::string username = usernameBuffer;
            std::string email = gpEmailBuffer;

            if (username.empty() || email.empty()) 
            {
                g_GetPassPopupMsgType = 2;
                g_GetPassPopupMessage = g_SelecteNgonNgu ? "Enter your account and email first!" : "Nhập tài khoản và email trước!";
            }
            else 
            {
                // Reset state, launch thread
                g_GetPassInProgress = true;
                g_GetPassResultReady = false;
                g_GetPassPopupMsgType = 3;  // loading
                g_GetPassPopupMessage = g_SelecteNgonNgu ? "Retrieving password..." : "Đang lấy mật khẩu...";

                std::thread(GetPassThreadFunc, username, email).detach();
            }
        }
    }

    ImGui::EndPopup();
}

void InitCall()
{
    strcpy(gpEmailBuffer, "");
    strcpy(usernameBuffer, "");
    strcpy(passwordBuffer, "");
    g_GetPassPopupMessage = "";
    g_GetPassPopupMsgType = 0;
    showGetPassPopup = true;
}
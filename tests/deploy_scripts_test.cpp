#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    EXPECT_TRUE(file.is_open()) << "Failed to open file: " << path;
    if (!file.is_open()) {
        return {};
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

}  // namespace

TEST(DeployScriptsTest, DebianPostinstInstallDoesNotPassBinaryPath) {
    const auto postinst = readFile(std::filesystem::path("..") / "deploy" / "debian" / "postinst");

    EXPECT_NE(postinst.find("/opt/fuelflux/deploy/install.sh install"), std::string::npos);
    EXPECT_EQ(postinst.find("/opt/fuelflux/deploy/install.sh install /opt/fuelflux/bin/fuelflux"),
              std::string::npos);
}

TEST(DeployScriptsTest, StandaloneInstallPathStillSupportsBinaryCopy) {
    const auto installScript = readFile(std::filesystem::path("..") / "deploy" / "install.sh");

    EXPECT_NE(installScript.find("install_binary \"${binary_path}\""), std::string::npos);
    EXPECT_NE(installScript.find("cp \"${binary_path}\" \"${BIN_DIR}/${SERVICE_NAME}\""), std::string::npos);
}

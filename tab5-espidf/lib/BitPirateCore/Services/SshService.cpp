#include "SshService.h"
#include <Arduino.h>

struct SshTaskParams {
    std::string host, user, pass;
    int verbosity;
    int port;
    SshService* service;
};

void SshService::startTask(const std::string& host, const std::string& user, const std::string& pass, int verbosity, int port) {
    auto* params = new SshTaskParams{host, user, pass, verbosity, port, this};
    xTaskCreatePinnedToCore(connectTask, "SSHConnect", 20000, params, 1, nullptr, 1);
    delay(2000); // let the task begins
}

void SshService::connectTask(void* pvParams) {
    auto* params = static_cast<SshTaskParams*>(pvParams);
    params->service->connect(params->host, params->user, params->pass, params->verbosity, params->port);
    delete params;
    vTaskDelete(nullptr);
}

bool SshService::connect(const std::string& host, const std::string& user, const std::string& pass, int verbosity, int port) {
    session = ssh_new();
    if (!session) return false;

    ssh_options_set(session, SSH_OPTIONS_HOST, host.c_str());
    ssh_options_set(session, SSH_OPTIONS_USER, user.c_str());
    ssh_options_set(session, SSH_OPTIONS_PORT, &port);
    ssh_options_set(session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);

    if (ssh_connect(session) != SSH_OK) {
        close();
        return false;
    }

    if (!authenticate(pass)) return false;
    if (!openChannel()) return false;
    if (!requestPty()) return false;
    if (!startShell()) return false;

    connected = true;
    return true;
}

bool SshService::authenticate(const std::string& password) {
    int rc = ssh_userauth_password(session, nullptr, password.c_str());
    if (rc != SSH_AUTH_SUCCESS) {
        return false;
    }
    return true;
}

bool SshService::openChannel() {
    channel = ssh_channel_new(session);
    if (!channel || ssh_channel_open_session(channel) != SSH_OK) {
        return false;
    }
    return true;
}

bool SshService::requestPty() {
    if (ssh_channel_request_pty(channel) != SSH_OK) {
        return false;
    }
    return true;
}

bool SshService::startShell() {
    if (ssh_channel_request_shell(channel) != SSH_OK) {
        return false;
    }
    return true;
}

bool SshService::isConnected() const {
    if (!connected || !session || !channel) return false;
    return ssh_channel_is_open(channel) && !ssh_channel_is_eof(channel);
}

void SshService::writeChar(char c) {
    if (!isConnected()) return;
    ssh_channel_write(channel, &c, 1);
}

std::string SshService::readOutput() {
    if (!isConnected()) return "";
    char buf[256];
    int n = ssh_channel_read(channel, buf, sizeof(buf), 0);
    return (n > 0) ? std::string(buf, n) : "";
}

std::string SshService::readOutputNonBlocking() {
    if (!isConnected()) return "";
    char buf[256];
    int n = ssh_channel_read_nonblocking(channel, buf, sizeof(buf), 0);
    return (n > 0) ? std::string(buf, n) : "";
}

void SshService::close() {
    if (channel) {
        ssh_channel_close(channel);
        ssh_channel_free(channel);
        channel = nullptr;
    }
    if (session) {
        ssh_disconnect(session);
        ssh_free(session);
        session = nullptr;
    }
    connected = false;
}

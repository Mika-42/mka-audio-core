#include <jack/jack.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <expected>
#include <fcntl.h>
#include <spawn.h>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <gtest/gtest.h>
import mka.audio.backend.abstract;
import mka.audio.backend.jack;
extern char** environ;
namespace {
using namespace mka::audio::core;
using namespace std::chrono_literals;
template<class P> bool waitFor(P p) {
    const auto end = std::chrono::steady_clock::now() + 5s;
    while (!p() && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(10ms);
    return p();
}
class TestServer {
    pid_t child = -1;
public:
    std::string name = "mka-s03-loss-" + std::to_string(getpid());
    ~TestServer() { stop(); }
    bool start() {
        char* args[]{const_cast<char*>("jackd"), const_cast<char*>("--name"),name.data(),
            const_cast<char*>("--no-realtime"),const_cast<char*>("-d"),const_cast<char*>("dummy"),
            const_cast<char*>("-r"),const_cast<char*>("48000"),const_cast<char*>("-p"),const_cast<char*>("256"),nullptr};
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions,STDOUT_FILENO,"/dev/null",O_WRONLY,0);
        posix_spawn_file_actions_adddup2(&actions,STDOUT_FILENO,STDERR_FILENO);
        const int error = posix_spawnp(&child,args[0],&actions,nullptr,args,environ);
        posix_spawn_file_actions_destroy(&actions);
        if (error != 0) { child = -1; return false; }
        return waitFor([&] {
            jack_status_t status{};
            auto* probe = jack_client_open("s03-loss-ready",JackNoStartServer,&status);
            if (!probe) return false;
            jack_client_close(probe); return true;
        });
    }
    void stop() {
        if (child <= 0) return;
        kill(child,SIGTERM);
        bool reaped = false;
        if (!waitFor([&] { if (!reaped) reaped = waitpid(child,nullptr,WNOHANG) == child; return reaped; })) {
            kill(child,SIGKILL); waitpid(child,nullptr,0);
        }
        child = -1;
    }
};
}
TEST(JackServerLoss, RealServerExitQuiescesAudioReportsFailureAndCanRecover) {
    TestServer server;
    ASSERT_EQ(setenv("JACK_DEFAULT_SERVER",server.name.c_str(),1),0);
    ASSERT_TRUE(server.start());
    JACK b; std::atomic<unsigned> calls{0}, failures{0};
    ASSERT_TRUE(b.setEventHandler(+[](void* u,const Event& e) noexcept {
        if (e.type == EventType::Failed) ++*static_cast<std::atomic<unsigned>*>(u);
    },&failures));
    ASSERT_TRUE(b.setProcessFunction(+[](void* u,const AudioProcessContext&) noexcept { ++*static_cast<std::atomic<unsigned>*>(u); },&calls));
    EndpointConfig cfg{"system",Direction::Output,0,2,48000,Format::Float32,256};
    ASSERT_TRUE(b.open(cfg)); ASSERT_TRUE(b.start());
    EXPECT_TRUE(waitFor([&] { return calls.load() > 10; }));
    server.stop();
    EXPECT_TRUE(waitFor([&] { return b.status().failed && failures.load() == 1; }));
    const auto count = calls.load(); std::this_thread::sleep_for(30ms);
    EXPECT_EQ(calls.load(),count);
    ASSERT_TRUE(b.stop()); ASSERT_TRUE(b.close());
    const auto offline = b.open(cfg); ASSERT_FALSE(offline);
    EXPECT_EQ(offline.error(),ErrorType::EndpointUnavailable);
    ASSERT_TRUE(server.start());
    ASSERT_TRUE(b.open(cfg)); ASSERT_TRUE(b.start());
    EXPECT_FALSE(b.status().failed);
    EXPECT_TRUE(waitFor([&] { return calls.load() > count + 10; }));
    ASSERT_TRUE(b.stop()); ASSERT_TRUE(b.close());
    EXPECT_EQ(failures.load(),1u);
}

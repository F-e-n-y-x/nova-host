/**
 * @file tests/unit/test_web_client.cpp
 * @brief Tests for the browser client sidecar: ports, command line, config edits and supervision.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

// local includes
#include <src/config.h>
#include <src/web_client.h>

#if defined(__linux__)
  #include <signal.h>
  #include <unistd.h>
#endif

using namespace std::chrono_literals;
namespace fs = std::filesystem;

TEST(WebClientTest, PortsFollowTheBasePort) {
  const auto l = web_client::make_launch(47989, "", false, "lan");
  EXPECT_EQ(l.gateway_port, 47995);
  EXPECT_EQ(l.upstream_port, 47996);
  EXPECT_EQ(l.udp_min, 49000);
  EXPECT_EQ(l.udp_max, 49019);
  EXPECT_EQ(l.nova_web_port, 47990);
  EXPECT_EQ(l.nova_http_port, 47989);
  EXPECT_EQ(l.listen_host, "0.0.0.0");
  EXPECT_EQ(l.nova_address, "127.0.0.1");

  const auto t = web_client::make_launch(57989, "", true, "lan");
  EXPECT_EQ(t.gateway_port, 57995);
  EXPECT_EQ(t.upstream_port, 57996);
  EXPECT_EQ(t.udp_min, 59000);
  EXPECT_EQ(t.listen_host, "::");
}

TEST(WebClientTest, NeverWiderThanLan) {
  EXPECT_EQ(web_client::make_launch(47989, "", false, "wan").allow, "lan");
  EXPECT_EQ(web_client::make_launch(47989, "", false, "lan").allow, "lan");
  EXPECT_EQ(web_client::make_launch(47989, "", false, "pc").allow, "pc");
  EXPECT_EQ(web_client::make_launch(47989, "", false, "garbage").allow, "lan");
}

TEST(WebClientTest, BindAddressIsUsedForListeningAndReachingNova) {
  const auto l = web_client::make_launch(47989, " 192.168.10.10 ", false, "lan");
  EXPECT_EQ(l.listen_host, "192.168.10.10");
  EXPECT_EQ(l.nova_address, "192.168.10.10");
  const auto any = web_client::make_launch(47989, "0.0.0.0", false, "lan");
  EXPECT_EQ(any.nova_address, "127.0.0.1");
}

TEST(WebClientTest, CommandLine) {
  auto l = web_client::make_launch(47989, "fd00::5", false, "pc");
  l.lib_dir = "/usr/lib/nova-host/web-client";
  l.state_dir = "/home/u/.config/nova-host/web-client";
  l.tls_cert = "/c.pem";
  l.tls_key = "/k.pem";
  const auto argv = web_client::command_line(l);
  ASSERT_GE(argv.size(), 3u);
  EXPECT_EQ(argv[0], "python3");
  EXPECT_EQ(argv[1], "/usr/lib/nova-host/web-client/gateway/nova_web_gateway.py");
  EXPECT_EQ(argv[2], "run");
  const auto after = [&](const std::string &flag) {
    for (std::size_t i = 0; i + 1 < argv.size(); ++i) {
      if (argv[i] == flag) {
        return argv[i + 1];
      }
    }
    return std::string {"<missing>"};
  };
  EXPECT_EQ(after("--listen-port"), "47995");
  EXPECT_EQ(after("--upstream-port"), "47996");
  EXPECT_EQ(after("--nova-address"), "fd00::5");
  EXPECT_EQ(after("--nova-address-url"), "[fd00::5]");
  EXPECT_EQ(after("--allow"), "pc");
  EXPECT_EQ(after("--tls-cert"), "/c.pem");
  EXPECT_EQ(after("--tls-key"), "/k.pem");
  EXPECT_EQ(after("--state-dir"), "/home/u/.config/nova-host/web-client");
}

TEST(WebClientTest, SetConfigLineReplacesAppendsAndKeepsTheRest) {
  EXPECT_EQ(web_client::set_config_line("", "web_client", "enabled"), "web_client = enabled\n");
  EXPECT_EQ(web_client::set_config_line("port = 47989\n", "web_client", "enabled"), "port = 47989\nweb_client = enabled\n");
  EXPECT_EQ(web_client::set_config_line("a = 1\nweb_client = disabled\nb = 2", "web_client", "enabled"), "a = 1\nweb_client = enabled\nb = 2\n");
  EXPECT_EQ(web_client::set_config_line("  web_client=on\nweb_client = off\n", "web_client", "disabled"), "web_client = disabled\n");
  EXPECT_EQ(web_client::set_config_line("# web_client = enabled\nweb_client_x = 1\n", "web_client", "enabled"),
            "# web_client = enabled\nweb_client_x = 1\nweb_client = enabled\n");
}

TEST(WebClientTest, OffByDefault) {
  EXPECT_FALSE(config::nvhttp.web_client);
}

#if defined(__linux__)
namespace {
  /**
   * @brief A fake sidecar: the gateway script records its pid, then sleeps or exits.
   */
  struct fake_sidecar_t {
    fs::path dir;

    explicit fake_sidecar_t(const std::string &body) {
      dir = fs::temp_directory_path() / ("nova-wc-test-" + std::to_string(::getpid()) + "-" + std::to_string(std::rand()));
      fs::create_directories(dir / "gateway");
      fs::create_directories(dir / "static");
      std::ofstream(dir / "web-server") << "";
      std::ofstream(dir / "streamer") << "";
      std::ofstream(dir / "gateway" / "nova_web_gateway.py") << "import os, sys, time\n"
                                                                << "open(os.path.join(os.path.dirname(__file__), 'pid'), 'a').write(str(os.getpid()) + '\\n')\n"
                                                                << body;
      ::setenv("NOVA_WEB_CLIENT_DIR", dir.c_str(), 1);
    }

    ~fake_sidecar_t() {
      web_client::shutdown();
      ::unsetenv("NOVA_WEB_CLIENT_DIR");
      std::error_code ec;
      fs::remove_all(dir, ec);
    }

    int starts() const {
      std::ifstream in(dir / "gateway" / "pid");
      int n = 0;
      std::string line;
      while (std::getline(in, line)) {
        ++n;
      }
      return n;
    }

    long last_pid() const {
      std::ifstream in(dir / "gateway" / "pid");
      long pid = -1;
      std::string line;
      while (std::getline(in, line)) {
        pid = std::stol(line);
      }
      return pid;
    }
  };

  template<class F>
  bool eventually(F f, std::chrono::milliseconds limit = 5000ms) {
    const auto end = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < end) {
      if (f()) {
        return true;
      }
      std::this_thread::sleep_for(50ms);
    }
    return f();
  }
}  // namespace

TEST(WebClientTest, NotInstalledWithoutTheSidecar) {
  ::setenv("NOVA_WEB_CLIENT_DIR", "/nonexistent/nova-web-client", 1);
  EXPECT_FALSE(web_client::status().installed);
  EXPECT_EQ(web_client::status().state, "not_installed");
  web_client::apply(true);
  web_client::apply(false);
  EXPECT_EQ(web_client::status().state, "not_installed");
  ::unsetenv("NOVA_WEB_CLIENT_DIR");
}

TEST(WebClientTest, StartsAndStopsTheSidecarProcessGroup) {
  fake_sidecar_t fake {"time.sleep(60)\n"};
  ASSERT_TRUE(web_client::status().installed);
  web_client::apply(true);
  ASSERT_TRUE(eventually([&] {
    return fake.starts() == 1;
  }));
  EXPECT_EQ(web_client::status().state, "running");
  const auto pid = fake.last_pid();
  EXPECT_EQ(::kill(pid, 0), 0);
  web_client::apply(true);  // already on: no second process
  std::this_thread::sleep_for(300ms);
  EXPECT_EQ(fake.starts(), 1);

  web_client::apply(false);
  EXPECT_EQ(web_client::status().state, "off");
  EXPECT_NE(::kill(pid, 0), 0);  // gone
}

TEST(WebClientTest, RestartsACrashedSidecar) {
  fake_sidecar_t fake {"sys.exit(3)\n"};
  web_client::apply(true);
  ASSERT_TRUE(eventually([&] {
    return fake.starts() >= 2;
  }));
  const auto s = web_client::status();
  EXPECT_GE(s.restarts, 1);
  EXPECT_EQ(s.last_error, "exited with code 3");
  web_client::apply(false);
  EXPECT_EQ(web_client::status().state, "off");
}
#endif

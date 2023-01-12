//
// Created by Aidan on 9/28/2021.
//

#pragma once

#include <client_https.hpp>

namespace discordpp {
using HttpsClient = SimpleWeb::Client<SimpleWeb::HTTPS>;

template <class BASE> class RestSimpleWeb : public BASE, virtual BotStruct {
    std::unique_ptr<asio::steady_timer> retry_;
    std::unique_ptr<HttpsClient> client_;

  public:
    void initBot(unsigned int apiVersionIn, const std::string &tokenIn,
                 std::shared_ptr<asio::io_context> aiocIn) override {
        BASE::initBot(apiVersionIn, tokenIn, aiocIn);
        client_ = std::make_unique<HttpsClient>("discord.com:443");
        client_->io_service = aiocIn;
    }
    void doCall(sptr<RenderedCall> call) override {
        std::ostringstream targetss;
        targetss << "/api/v" << apiVersion << *call->target;
        auto target = targetss.str();
        SimpleWeb::CaseInsensitiveMultimap headers{{"authorization", token},{"X-RateLimit-Precision", "millisecond"}, {"User-Agent", user_agent_}};
        if (call->type) {
            headers.insert({"Content-Type", *call->type});
        }
        client_->request(
            *call->method, target, call->body ? *call->body : "",
            headers,
            [this, call,
             target](std::shared_ptr<HttpsClient::Response> response,
                     const SimpleWeb::error_code &ec) {
                if (ec) {
                    if (call->onRead)
                        aioc->post([call] { (*call->onRead)(true, {}); });
                    return fail(ec, "read", call);
                }

                json jres;
                {
                    std::ostringstream ss;
                    ss << response->content.rdbuf();
                    const std::string &body = ss.str();
                    log::log(log::trace, [body](std::ostream *log) {
                        *log << "Received: " << body << '\n';
                    });
                    if (!body.empty()) {
                        if (body.at(0) == '{' || body.at(0) == '[') {
                            jres = {{"body", json::parse(body)}};
                        } else {
                            std::cerr
                                << "Discord replied:\n"
                                << ss.str() << "\nTo the following target:\n"
                                << target << "\nWith the following payload:\n"
                                << *call->body << std::endl;
                        }
                    }
                }

                int status = std::stoi(response->status_code);
                jres["result"] = status;

                for (auto h :
                     {"X-RateLimit-Global", "X-RateLimit-Limit",
                      "X-RateLimit-Remaining", "X-RateLimit-Reset",
                      "X-RateLimit-Reset-After", "X-RateLimit-Bucket"}) {
                    auto it = response->header.find(h);
                    if (it != response->header.end()) {
                        jres["header"][h] = it->second;
                    }
                }

                if (jres.find("embed") != jres.end()) {
                    std::cout << "Discord API didn't like the following parts "
                                 "of your "
                                 "embed: ";
                    bool first = true;
                    for (const json &part : jres["embed"]) {
                        if (first) {
                            first = false;
                        } else {
                            std::cout << ", ";
                        }
                        std::cout << part.get<std::string>();
                    }
                    std::cout << std::endl;
                }

                log::log(log::debug, [call, jres](std::ostream *log) {
                    *log << "Read " << call->target << jres.dump(4) << '\n';
                });

                if (status < 200 || status >= 300) {
                    log::log(log::error, [call, jres](std::ostream *log) {
                        *log << "Discord sent an error! " << call->target
                             << jres.dump(4) << '\n';
                    });
                }

                if (call->onRead) {
                    aioc->post([call, jres, status]() {
                        (*call->onRead)(status < 200 || status >= 300, jres);
                    });
                }
            });
        if (call->onWrite) {
            aioc->post([call]() {
                (*call->onWrite)(false);
            });
        }
    }

    // Report a failure
    static void fail(SimpleWeb::error_code ec, char const *what,
                     sptr<RenderedCall> call) {
        if (std::string(what) != "shutdown" &&
            ec.message() != "stream truncated") {
            log::log(log::error, [ec, what, call](std::ostream *log) {
                *log << what << ": " << ec.message();
                if (call != nullptr) {
                    *log << ' ' << *call->target;
                    if (call->body) {
                        *log << call->body;
                    } else {
                        *log << " with no body";
                    }
                }
                *log << '\n';
            });
        }
    }
};
} // namespace discordpp
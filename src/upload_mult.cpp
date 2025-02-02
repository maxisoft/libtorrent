#include "libtorrent/upload_mult.hpp"
#include "libtorrent/torrent.hpp"

namespace libtorrent { // NOLINT(modernize-concat-nested-namespaces)

    namespace upload_mod {


        using detail::change_uploaded_counter_key;
        using detail::change_uploaded_counter_context;

        long long
        UploadMod::change_uploaded_counter(torrent &torrent, const long long total_payload_upload, long long bytes_downloaded) {
            if (upload_mult == 0)
            {
                read_env();
            }
            const auto now = aux::time_now32();
            acquire_lock();
            auto it = contexts.find(change_uploaded_counter_key(torrent));

            if (it == contexts.end()) {
                change_uploaded_counter_context ctx;
                ctx.last_time = torrent.started();
                if (ctx.last_time == decltype(ctx.last_time){}) {
                    ctx.last_time = now;
                }
                it = contexts.emplace(change_uploaded_counter_key(torrent), ctx).first;
            }
            release_lock();

            // do a local copy as it may not be thread safe to use memory ref
            change_uploaded_counter_context ctx = it->second;
            ctx.last_time = std::max(ctx.last_time, torrent.started());
            long long current_upload_mult = upload_mult;
            if (random_percent > 0) {
                std::normal_distribution<float> dist(0, static_cast<float>(random_percent) / 100.f);
                current_upload_mult += static_cast<decltype(current_upload_mult)>(
                        static_cast<float>(current_upload_mult) *
                        dist(random_engine));
                current_upload_mult = std::max(current_upload_mult, decltype(current_upload_mult)(1));
                current_upload_mult = std::min(current_upload_mult, upload_mult + upload_mult * random_percent / 100);
            }


            long long res = total_payload_upload * current_upload_mult / upload_mult_precision;

            if (minimal_ratio > 0 && res > 0 && bytes_downloaded > 0)
            {
                long long addition = bytes_downloaded * minimal_ratio / upload_mult_precision;
                addition -= res;
                addition = std::max(addition, decltype(addition)(0));
                if (random_percent > 0 && addition * random_percent > 100) {
                    std::uniform_int_distribution<decltype(addition)> dist(0,
                                                                                random_percent * addition / 100);
                    addition -= dist(random_engine);
                    addition = std::max(addition, decltype(addition)(0));
                }

                res += addition;
            }

            if (res > 0 && max_bandwidth > 0 && now != ctx.last_time) {
                //comply with max bandwidth
                auto bw = std::abs(total_seconds(now - ctx.last_time)) * max_bandwidth;
                if (random_percent > 0 && bw * random_percent > 100) {
                    std::uniform_int_distribution<decltype(max_bandwidth)> dist(0,
                                                                                random_percent * upload_mult_precision);
                    bw -= bw * dist(random_engine) / upload_mult_precision / 100;
                }
                if (bw > 0) {
                    res = std::min(res, bw);
                }
            }

            ctx.last_time = aux::time_now32();
            ctx.prev_total_payload_upload = total_payload_upload;
            acquire_lock();
            contexts[change_uploaded_counter_key(torrent)] = ctx;
            release_lock();

            if (std_err_log) {
                fprintf(stderr, "upload_scale: [%s %lld -> %lld]\n",
                        torrent.name().c_str(),
                        total_payload_upload,
                        res);
            }

            res = std::max(res, total_payload_upload);
#ifndef TORRENT_DISABLE_LOGGING
            if (res != total_payload_upload && torrent.should_log()) {
                torrent.debug_log("*** total_payload_upload: [%lld -> %lld] ",
                                  total_payload_upload, res);
            }
#endif

            return res;
        }

        void UploadMod::read_env() {
            if (std_err_log < 0) {
                std_err_log = std::getenv("LIB_TORRENT_STD_ERR_LOG") ? 1 : 0;
            }

            if (max_bandwidth == std::numeric_limits<long long>::max() || max_bandwidth == 0) {
                if (const char *env_p = std::getenv("LIB_TORRENT_UPLOAD_MAX_BANDWIDTH")) {
                    max_bandwidth = static_cast<decltype(max_bandwidth)>(std::atoll(env_p));
                    if (max_bandwidth == 0) {
                        max_bandwidth = std::numeric_limits<long long>::max() >> 1;
                    } else if (max_bandwidth > 0) {
                        max_bandwidth <<= 10; // convert kb to bytes
                        if (std_err_log) {
                            fprintf(stderr, "max_bandwidth: [%lld]\n", max_bandwidth);
                        }
                    }
                } else {
                    max_bandwidth = std::numeric_limits<long long>::max() >> 1;
                }
            }

            if (random_percent == std::numeric_limits<int>::min() || random_percent == 0) {
                if (const char *env_p = std::getenv("LIB_TORRENT_UPLOAD_RANDOMIZE_PERCENT")) {
                    random_percent = static_cast<decltype(random_percent)>(std::atoi(env_p));
                    if (random_percent == 0) {
                        random_percent = 5;
                    } else if (std_err_log) {
                        fprintf(stderr, "random_percent: [%d]\n", random_percent);
                    }
                }
            }

            if (upload_mult <= 0) {
                if (const char *env_p = std::getenv("LIB_TORRENT_UPLOAD_MULT")) {
                    upload_mult = static_cast<decltype(upload_mult)>(std::atof(env_p) *
                                                                     static_cast<double>(upload_mult_precision));
                } else {
                    upload_mult = upload_mult_precision;
                }
                if (std_err_log) {
                    fprintf(stderr, "upload_mult: [%.03f]\n", upload_mult_double());
                }
            }

            if (upload_mult <= 0) {
                upload_mult = upload_mult_precision;
            }

            if (minimal_ratio == std::numeric_limits<long long>::min() || minimal_ratio == 0)
            {
                if (const char *env_p = std::getenv("LIB_TORRENT_MINIMAL_RATIO")) {
                    minimal_ratio = static_cast<decltype(minimal_ratio)>(std::atof(env_p) *
                                                                     static_cast<double>(upload_mult_precision));
                } else {
                    minimal_ratio = -1;
                }
                if (std_err_log) {
                    fprintf(stderr, "minimal_ratio: [%.03f]\n", minimal_ratio_double());
                }
            }

        }

        bool UploadMod::cleanup(torrent &torrent) {
            bool res = false;
            acquire_lock();
            auto it = contexts.find(change_uploaded_counter_key(torrent));
            if (it != contexts.end()) {
                res = true;
                contexts.erase(it);
            }
            release_lock();
            return res;
        }

        namespace detail {
            change_uploaded_counter_key::change_uploaded_counter_key(torrent &torrent) : torrent_ptr(
                    reinterpret_cast<std::uintptr_t>(std::addressof(torrent))),
                    name(torrent.name()),
                    info_hash(torrent.info_hash()) {
            }
        }
    }
}


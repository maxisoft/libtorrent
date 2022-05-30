//
// Created by maxisoft on 30/05/2022.
//

#ifndef LIBTORRENT_UPLOAD_MULT_HPP
#define LIBTORRENT_UPLOAD_MULT_HPP


#include <cstdarg> // for va_list
#include <ctime>
#include <algorithm>
#include <set>
#include <map>
#include <vector>
#include <cctype>
#include <numeric>
#include <limits> // for numeric_limits
#include <cstdio> // for snprintf
#include <functional>
#include <unordered_map>
#include <cstdlib>
#include <atomic>
#include <cmath>
#include <random>


#include "libtorrent/info_hash.hpp" // for std::hash<libtorrent::info_hash_t>
#include "libtorrent/time.hpp"


namespace libtorrent {
    struct torrent;

    namespace upload_mod {

        namespace detail {
            struct change_uploaded_counter_context { // NOLINT(cppcoreguidelines-pro-type-member-init)
                change_uploaded_counter_context() = default;

                time_point32 last_time;
                std::int64_t prev_total_payload_upload;
            };

            struct change_uploaded_counter_key { // NOLINT(cppcoreguidelines-pro-type-member-init)
                std::uintptr_t torrent_ptr;
                std::string name;
                info_hash_t info_hash;

                change_uploaded_counter_key() = default;

                explicit change_uploaded_counter_key(torrent &torrent);

                inline bool operator==(const change_uploaded_counter_key &other) const {
                    if (torrent_ptr == other.torrent_ptr && name == other.name) {
                        return true;
                    }
                    return info_hash == other.info_hash;
                }

                struct HashFunction {
                    std::size_t operator()(change_uploaded_counter_key const &k) const {
                        return std::hash<libtorrent::info_hash_t>{}(k.info_hash);
                    }
                };
            };
        }

        class UploadMod {
            std::int64_t upload_mult;
            int random_percent;
            std::int64_t max_bandwidth;
            std::int8_t std_err_log;

            // use a spinlock, assuming the map's alloc/free are relatively fast and safe
            // (hint: there is no strict guarantees :) )
            std::atomic_bool _spin_lock;
            std::unordered_map<detail::change_uploaded_counter_key, detail::change_uploaded_counter_context, detail::change_uploaded_counter_key::HashFunction> contexts;
            std::default_random_engine random_engine;

            static constexpr std::int64_t upload_mult_precision = 1024;

        private:
            explicit UploadMod() :
                    upload_mult(std::numeric_limits<std::int64_t>::min()),
                    random_percent(std::numeric_limits<int>::min()),
                    max_bandwidth(std::numeric_limits<std::int64_t>::max()),
                    std_err_log(-1),
                    _spin_lock(),
                    contexts(),
                    random_engine(std::random_device()()) {
                read_env();
            }

            //UploadMod(UploadMod const&); // Don't Implement
            //void operator=(UploadMod const&); // Don't implement
        public:
            UploadMod(UploadMod const &) = delete;

            void operator=(UploadMod const &) = delete;

        public:

            double upload_mult_double() const {
                return static_cast<double>(upload_mult) / static_cast<double>(upload_mult_precision);
            }

            std::int64_t change_uploaded_counter(torrent &torrent, std::int64_t total_payload_upload);

            bool cleanup(torrent &torrent);

            static UploadMod &instance() {
                static UploadMod _inst;
                return std::ref(_inst);
            }


        private:

            void read_env();

            inline void acquire_lock() {
                bool expected = false;
                while (!_spin_lock.compare_exchange_weak(expected, true, std::memory_order_acquire)) {
                    expected = false;
                }
            }

            inline void release_lock() {
                _spin_lock.store(false, std::memory_order_release);
            }

        };

    }
}
#endif //LIBTORRENT_UPLOAD_MULT_HPP

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
                long long prev_total_payload_upload;
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
            long long upload_mult;
            int random_percent;
            long long max_bandwidth;
            long long minimal_ratio;
            std::int8_t std_err_log;

            // use a spinlock, assuming the map's alloc/free are relatively fast and safe
            // (hint: there is no strict guarantees :) )
            std::atomic_bool spin_lock;
            std::unordered_map<detail::change_uploaded_counter_key, detail::change_uploaded_counter_context, detail::change_uploaded_counter_key::HashFunction> contexts;
            std::default_random_engine random_engine;

            static constexpr long long upload_mult_precision = 1024;

        private:
            explicit UploadMod() :
                    upload_mult(std::numeric_limits<long long>::min()),
                    random_percent(std::numeric_limits<int>::min()),
                    max_bandwidth(std::numeric_limits<long long>::max()),
                    minimal_ratio(),
                    std_err_log(-1),
                    spin_lock(),
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

            inline double upload_mult_double() const {
                return static_cast<double>(upload_mult) / static_cast<double>(upload_mult_precision);
            }

            inline double minimal_ratio_double() const {
                return static_cast<double>(minimal_ratio) / static_cast<double>(upload_mult_precision);
            }

            long long change_uploaded_counter(torrent &torrent, long long total_payload_upload, long long bytes_downloaded);

            bool cleanup(torrent &torrent);

            static UploadMod &instance() {
                static UploadMod _inst;
                return std::ref(_inst);
            }


        private:

            void read_env();

            inline void acquire_lock() {
                bool expected = false;
                while (!spin_lock.compare_exchange_weak(expected, true, std::memory_order_acquire)) {
                    expected = false;
                }
            }

            inline void release_lock() {
                spin_lock.store(false, std::memory_order_release);
            }

        };

    }
}
#endif //LIBTORRENT_UPLOAD_MULT_HPP

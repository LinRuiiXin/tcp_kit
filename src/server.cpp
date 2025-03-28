#include <network/server.h>
#include <error/errors.h>
#include <event2/buffer.h>
#include <stdint.h>

namespace tcp_kit {

    server_base::server_base(std::shared_ptr<filter_chain> filters_): _ctl(NEW), _filters(filters_) { }

    void server_base::trans_to(uint32_t rs) {
        std::unique_lock<std::mutex> lock(_mutex);
        _ctl.store(ctl_of(rs, handlers_map()), std::memory_order_release);
        _state.notify_all();
    }

    void server_base::wait_at_least(uint32_t rs) {
        std::unique_lock<std::mutex> lock(_mutex);
        while(!run_state_at_least(rs)) {
            interruptible_wait(_state, lock);
        }
    }

    inline uint32_t server_base::handlers_map() {
        return _ctl & ((1 << STATE_OFFSET) - 1);
    }

    inline uint32_t server_base::ctl_of(uint32_t rs, uint32_t hp) {
        return rs | hp;
    }

    inline uint32_t server_base::run_state_of(uint32_t rs) {
        return _ctl & (RUN_STATE_CAPACITY << STATE_OFFSET);
    }

    bool server_base::is_running() {
        return RUNNING == run_state_of(_ctl);
    }

    inline bool server_base::run_state_at_least(uint32_t rs) {
        return _ctl.load(std::memory_order_acquire) >= rs;
    }

    // -----------------------------------------------------------------------------------------------------------------

    ev_handler_base::ev_handler_base(): n_handler(0), accept_ev(nullptr) { }

    void ev_handler_base::bind_and_run(server_base* server_ptr) {
        assert(server_ptr);
        _server_base = server_ptr;
        _filters = _server_base->_filters;
        accept_ev = init(server_ptr);
        _server_base->try_ready();
        _server_base->wait_at_least(server_base::RUNNING);
        //log_debug("Event handler_base running...");
        run();
    }

    void ev_handler_base::call_conn_filters(ev_context* ctx) {
        try {
            _filters->connects(ctx);
        } catch (...) {
            // TODO
        }
    }

    inline size_t max(const size_t& a, const size_t& b) {
        return a > b ? a : b;
    }

    void ev_handler_base::register_read_write_filters(ev_context* ctx) {
        auto &reads = _filters->reads;
        auto &writes = _filters->writes;
        for(size_t i = 0; i < max(reads.size(), writes.size()); ++i) {
            bufferevent* nested_bev = bufferevent_filter_new(ctx->bev,
                                                             i < reads.size() ? reads[i] : nullptr,
                                                             i < writes.size() ? writes[i] : nullptr,
                                                             BEV_OPT_CLOSE_ON_FREE, nullptr, ctx);
            if(nested_bev) {
                ctx->bev = nested_bev;
            } else {
                throw generic_error<CONS_BEV_FAILED>("Failed to register filter with index [%d]", i);
            }
        }
    }

    void ev_handler_base::call_close_filters(ev_context* ctx) {
        try {
            _filters->closes(ctx);
        } catch (...) {
            // TODO
        }
    }

    // std::unique_ptr<evbuffer_holder> ev_handler_base::call_process_filters(ev_context *ctx) {
    //     auto holder = std::make_unique<evbuffer_holder>(bufferevent_get_input(ctx->bev));
    //     return _filters->process(ctx, move(holder));
    // }

    // -----------------------------------------------------------------------------------------------------------------

    void handler_base::bind_and_run(server_base* server_ptr) {
        assert(server_ptr);
        _server_base = server_ptr;
        _filters = _server_base->_filters;
        msg_queue = std::move(race ? std::unique_ptr<queue<msg>>(new lock_free_queue<msg>())
                                   : std::unique_ptr<queue< msg>>(new lock_free_spsc_queue<msg>()));
        init(server_ptr);
        _server_base->try_ready();
        _server_base->wait_at_least(server_base::RUNNING);
        //log_debug("Handler running...");
        run();
    }

    std::unique_ptr<msg_buffer> handler_base::make_msg_buffer(char *line_msg, size_t len) {
        return std::unique_ptr<msg_buffer>(new msg_buffer(line_msg, len));
    }

//    std::unique_ptr<evbuffer_holder> handler_base::call_process_filters(ev_context *ctx) {
//        auto holder = std::make_unique<evbuffer_holder>(bufferevent_get_input(ctx->bev));
//        return _filters->process(ctx, move(holder));
//    }

    handler_base::~handler_base() {

    }

}
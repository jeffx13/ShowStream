#pragma once
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
#include <cstring>
#include <stdexcept>
#include <assert.h>

namespace Mpv
{
class Node : mpv_node
{
public:
    typedef Node *iterator;

    inline Node() noexcept
    {
        format = MPV_FORMAT_NONE;
    }

    inline Node(Node &&other) noexcept
    {
        format = other.format;
        u = other.u;
        other.format = MPV_FORMAT_NONE;
    }

    inline Node(bool v) noexcept
    {
        format = MPV_FORMAT_FLAG;
        u.flag = v;
    }

    inline Node(int64_t v) noexcept
    {
        format = MPV_FORMAT_INT64;
        u.int64 = v;
    }

    inline Node(double v) noexcept
    {
        format = MPV_FORMAT_DOUBLE;
        u.double_ = v;
    }

    inline Node(const char *v) noexcept
    {
        format = MPV_FORMAT_STRING;
        u.string = const_cast<char*>(v);
    }

    inline mpv_format type() const noexcept
    {
        return format;
    }

    inline operator bool() const noexcept
    {
        assert(format == MPV_FORMAT_FLAG);
        return u.flag;
    }

    inline operator int64_t() const noexcept
    {
        assert(format == MPV_FORMAT_INT64);
        return u.int64;
    }

    inline operator double() const noexcept
    {
        assert(format == MPV_FORMAT_DOUBLE);
        return u.double_;
    }

    inline operator const char*() const noexcept
    {
        assert(format == MPV_FORMAT_STRING);
        return u.string;
    }

    inline int size() const noexcept
    {
        assert(format == MPV_FORMAT_NODE_ARRAY);
        return u.list->num;
    }

    inline const Node &operator[](int i) const noexcept
    {
        assert(format == MPV_FORMAT_NODE_ARRAY);
        return static_cast<Node *>(u.list->values)[i];
    }

    inline iterator begin() const noexcept
    {
        assert(format == MPV_FORMAT_NODE_ARRAY);
        return static_cast<Node *>(&u.list->values[0]);
    }

    inline iterator end() const noexcept
    {
        assert(format == MPV_FORMAT_NODE_ARRAY);
        return static_cast<Node *>(&u.list->values[u.list->num]);
    }

    inline const Node &operator[](const char *key) const
    {
        assert(format == MPV_FORMAT_NODE_MAP);
        for (int i = 0; i < u.list->num; i++)
        {
            if (std::strcmp(key, u.list->keys[i]) == 0)
            {
                return static_cast<Node *>(u.list->values)[i];
            }
        }
        throw std::runtime_error("Mpv::NodeMap::operator[]: key does not exist!");
    }

    inline const mpv_node_list *list() const noexcept
    {
        assert(format == MPV_FORMAT_NODE_ARRAY || format == MPV_FORMAT_NODE_MAP);
        return u.list;
    }
};

class Handle
{
private:
    mpv_handle *m_handle = nullptr;
    mpv_render_context *m_rctx = nullptr;

public:
    inline Handle() noexcept
    {
        m_handle = mpv_create();
    }

    inline ~Handle() noexcept
    {
        if (m_rctx)
        {
            mpv_render_context_set_update_callback(m_rctx, nullptr, nullptr);
        }
        mpv_set_wakeup_callback(m_handle, nullptr, nullptr);

        if (m_rctx)
        {
            mpv_render_context_free(m_rctx);
        }
        mpv_terminate_destroy(m_handle);
    }

    inline int initialize() const noexcept
    {
        return mpv_initialize(m_handle);
    }

    inline int set_option(const char *name, const Node& data) const noexcept
    {
        return mpv_set_option(m_handle, name, MPV_FORMAT_NODE, const_cast<Node*>(&data));
    }

    inline int command_async(const char **args) const noexcept
    {
        return mpv_command_async(m_handle, 0, args);
    }

    inline int command(const char **args) const noexcept
    {
        return mpv_command(m_handle, args);
    }

    inline int set_property_async(const char *name, const Node& data) const noexcept
    {
        return mpv_set_property_async(m_handle, 0, name, MPV_FORMAT_NODE, const_cast<Node*>(&data));
    }

    inline int set_property(const char *name, const Node& data) const noexcept
    {
        return mpv_set_property(m_handle, name, MPV_FORMAT_NODE, const_cast<Node*>(&data));
    }

    inline Node get_property(const char *name) const noexcept
    {
        Node tmp;
        mpv_get_property(m_handle, name, MPV_FORMAT_NODE, &tmp);
        return tmp;
    }

    // `id` comes back as the property-change event's reply_userdata, which is how the caller
    // tells the properties apart without comparing names.
    inline int observe_property(uint64_t id, const char *name) const noexcept
    {
        return mpv_observe_property(m_handle, id, name, MPV_FORMAT_NODE);
    }

    inline const mpv_event *wait_event(double timeout = 0) const noexcept
    {
        return mpv_wait_event(m_handle, timeout);
    }

    inline void set_wakeup_callback(void (*callback)(void *userdata), void *userdata) const noexcept
    {
        mpv_set_wakeup_callback(m_handle, callback, userdata);
    }

    inline int request_log_messages(const char *min_level) const noexcept
    {
        return mpv_request_log_messages(m_handle, min_level);
    }


    inline int renderer_initialize(mpv_render_param *params) noexcept
    {
        return mpv_render_context_create(&m_rctx, m_handle, params);
    }

    inline void set_render_callback(mpv_render_update_fn callback, void *userdata) const noexcept
    {
        mpv_render_context_set_update_callback(m_rctx, callback, userdata);
    }

    inline void render(mpv_render_param *params) const noexcept
    {
        mpv_render_context_render(m_rctx, params);
    }


    // Must run on the thread whose GL context created the renderer, with it current.
    inline void free_renderer() noexcept
    {
        if (m_rctx) {
            mpv_render_context_set_update_callback(m_rctx, nullptr, nullptr);
            mpv_render_context_free(m_rctx);
            m_rctx = nullptr;
        }
    }
};
}

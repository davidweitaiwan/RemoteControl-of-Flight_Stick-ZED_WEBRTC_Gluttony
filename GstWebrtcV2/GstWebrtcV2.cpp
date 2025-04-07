/*
gst-launch-1.0 -v ksvideosrc do-stats=TRUE ! videoconvert ! x264enc speed-preset=veryfast tune=zerolatency ! h264parse ! nvh264dec ! autovideosink
 */
#include <gst/gst.h>
#include <gst/sdp/sdp.h>
#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>
#include<stdio.h>
 /* For signalling */
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <string.h>
#include <gst/video/video.h>
 /* share_mode 0 is videotestsrc (windows / linux, VP8), 1 is webcam (Windows, VP8), 2 is webcam (linux, VP8),
    3 is webcam (linux, H264) 4 is zed camera(linux, H264) */

#define default_share_mode 4
#define USE_UDP 1

typedef struct {
    GMainLoop* loop;
    GstElement* pipeline;
    GList* peers;
    GList* send_offer;
    SoupWebsocketConnection* ws_conn;
    enum AppState app_state;
    gchar* server_url;
    gchar* local_id;
    gchar* room_id;
    gint udp_port;
    gint share_mode;
    gboolean strict_ssl;
} AppContext;


enum AppState
{
    APP_STATE_UNKNOWN = 0,
    APP_STATE_ERROR = 1,          /* generic error */
    SERVER_CONNECTING = 1000,
    SERVER_CONNECTION_ERROR,
    SERVER_CONNECTED,             /* Ready to register */
    SERVER_REGISTERING = 2000,
    SERVER_REGISTRATION_ERROR,
    SERVER_REGISTERED,            /* Ready to call a peer */
    SERVER_CLOSED,                /* server connection closed by us or the server */
    ROOM_JOINING = 3000,
    ROOM_JOIN_ERROR,
    ROOM_JOINED,
    ROOM_CALL_NEGOTIATING = 4000, /* negotiating with some or all peers */
    ROOM_CALL_OFFERING,           /* when we're the one sending the offer */
    ROOM_CALL_ANSWERING,          /* when we're the one answering an offer */
    ROOM_CALL_STARTED,            /* in a call with some or all peers */
    ROOM_CALL_STOPPING,
    ROOM_CALL_STOPPED,
    ROOM_CALL_ERROR,
};

static GMainLoop* loop;
static GstElement* pipeline;
static GList* peers;
static GList* send_offer;

static SoupWebsocketConnection* ws_conn = NULL;
static enum AppState app_state = (AppState)0;
static const gchar* default_server_url = "ws://rtc.o3o.tw:8443";
static gchar* server_url = NULL;
static gchar* local_id = NULL;
static gchar* room_id = NULL;
static gint* share_mode = (int*)default_share_mode;
// static gboolean strict_ssl = TRUE;
static gboolean strict_ssl = FALSE;
static gint udp_port = 5000;

static GOptionEntry entries[] = {
  {"name", 0, 0, G_OPTION_ARG_STRING, &local_id,
      "Name we will send to the server", "ID"},
  {"room-id", 0, 0, G_OPTION_ARG_STRING, &room_id,
      "Room name to join or create", "ID"},
  {"server", 0, 0, G_OPTION_ARG_STRING, &server_url,
      "Signalling server to connect to", "URL"},
  {"udp-port", 0, 0, G_OPTION_ARG_INT, &udp_port,
      "Broadcast the input streaming to specific port", "PORT"},
  {"share_mode", 0, 0, G_OPTION_ARG_INT, &share_mode,
      "Broadcast the input streaming to specific share_mode", "share_mode"},
  {NULL}
};



static void
display_list(GList* list)
{
    GList* iterator = NULL;
    for (iterator = list; iterator; iterator = iterator->next) {
        g_print(" %s ", (char*)iterator->data);
    }
}

static void
find_peer_send_offer(AppContext* ctx, const gchar* peer_id)
{
    gint i;
    JsonParser* parser = json_parser_new();
    JsonNode* node;
    JsonObject* obj;
    JsonObject* peer;
    JsonArray* array;

    if (!json_parser_load_from_file(parser, "schedule.json", NULL)) {
        gst_printerr("Failed to open schedule.json\n");
        g_object_unref(parser);
        return;
    }

    node = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(node)) {
        gst_printerr("Invalid JSON root object\n");
        g_object_unref(parser);
        return;
    }

    obj = json_node_get_object(node);
    peer = json_object_get_object_member(obj, peer_id);

    if (!peer) {
        gst_printerr("Peer ID %s not found in schedule.json\n", peer_id);
        g_object_unref(parser);
        return;
    }

    array = json_object_get_array_member(peer, "send_offer");
    if (!array) {
        gst_printerr("No send_offer array found for peer %s\n", peer_id);
        g_object_unref(parser);
        return;
    }

    for (i = 0; i < json_array_get_length(array); i++) {
        const gchar* offer_peer = json_array_get_string_element(array, i);
        ctx->send_offer = g_list_prepend(ctx->send_offer, g_strdup(offer_peer));
        g_print("My send offer peer-id is %s.\n", offer_peer);
    }

    g_object_unref(parser);
}


static gint
compare_str_glist(gconstpointer a, gconstpointer b)
{
    return g_strcmp0((char*)a, (char*)b);
}


// static const gchar *
// check_send_offer_peer_list (const gchar * peer_id)
// {
//   return (g_list_find_custom (peers, peer_id, compare_str_glist))->data;
// }

static const gchar*
find_peer_from_list(GList* peers, const gchar* peer_id)
{
    GList* found = g_list_find_custom(peers, peer_id, compare_str_glist);
    return found ? (const gchar*)found->data : NULL;
}
static gboolean
cleanup_and_quit_loop(AppContext* ctx, const gchar* msg, enum AppState state)
{
    if (msg)
        gst_printerr("%s\n", msg);
    if (state > 0)
        ctx->app_state = state;

    if (ctx->ws_conn) {
        if (soup_websocket_connection_get_state(ctx->ws_conn) == SOUP_WEBSOCKET_STATE_OPEN)
            soup_websocket_connection_close(ctx->ws_conn, 1000, "");
        else
            g_object_unref(ctx->ws_conn);
        ctx->ws_conn = NULL;
    }

    if (ctx->loop) {
        g_main_loop_quit(ctx->loop);
        ctx->loop = NULL;
    }

    return G_SOURCE_REMOVE;
}

static gchar*
get_string_from_json_object(JsonObject* object)
{
    JsonNode* root;
    JsonGenerator* generator;
    gchar* text;

    /* Make it the root node */
    root = json_node_init_object(json_node_alloc(), object);
    generator = json_generator_new();
    json_generator_set_root(generator, root);
    text = json_generator_to_data(generator, NULL);

    /* Release everything */
    g_object_unref(generator);
    json_node_free(root);
    return text;
}

static void
handle_media_stream(GstPad* pad, GstElement* pipe, const char* convert_name,
    const char* sink_name)
{
    GstPad* qpad;
    GstElement* q, * conv, * sink;
    GstPadLinkReturn ret;

    q = gst_element_factory_make("queue", NULL);
    g_assert_nonnull(q);
    conv = gst_element_factory_make(convert_name, NULL);
    g_assert_nonnull(conv);
    sink = gst_element_factory_make(sink_name, NULL);
    g_assert_nonnull(sink);
    gst_bin_add_many(GST_BIN(pipe), q, conv, sink, NULL);
    gst_element_sync_state_with_parent(q);
    gst_element_sync_state_with_parent(conv);
    gst_element_sync_state_with_parent(sink);
    gst_element_link_many(q, conv, sink, NULL);

    qpad = gst_element_get_static_pad(q, "sink");

    ret = gst_pad_link(pad, qpad);
    g_assert_cmpint(ret, == , GST_PAD_LINK_OK);
}

static void
on_incoming_decodebin_stream(GstElement* decodebin, GstPad* pad,
    GstElement* pipe)
{
    GstCaps* caps;
    const gchar* name;

    if (!gst_pad_has_current_caps(pad)) {
        gst_printerr("Pad '%s' has no caps, can't do anything, ignoring\n",
            GST_PAD_NAME(pad));
        return;
    }

    caps = gst_pad_get_current_caps(pad);
    name = gst_structure_get_name(gst_caps_get_structure(caps, 0));



    if (g_str_has_prefix(name, "video")) {
        handle_media_stream(pad, pipe, "videoconvert", "autovideosink");
    }
    else if (g_str_has_prefix(name, "audio")) {
        handle_media_stream(pad, pipe, "audioconvert", "autoaudiosink");
    }
    else {
        gst_printerr("Unknown pad %s, ignoring", GST_PAD_NAME(pad));
    }
}

static void
on_incoming_stream(GstElement* webrtc, GstPad* pad, GstElement* pipe)
{
    GstElement* decodebin;
    GstPad* sinkpad;

    if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC)
        return;

    decodebin = gst_element_factory_make("decodebin", NULL);
    g_signal_connect(decodebin, "pad-added",
        G_CALLBACK(on_incoming_decodebin_stream), pipe);
    gst_bin_add(GST_BIN(pipe), decodebin);
    gst_element_sync_state_with_parent(decodebin);

    sinkpad = gst_element_get_static_pad(decodebin, "sink");
    gst_pad_link(pad, sinkpad);
    gst_object_unref(sinkpad);
}
static void send_room_peer_msg(AppContext* ctx, const gchar* text, const gchar* peer_id) {
    gchar* msg = g_strdup_printf("ROOM_PEER_MSG %s %s", peer_id, text);
    soup_websocket_connection_send_text(ctx->ws_conn, msg);
    g_free(msg);
}
typedef struct {
    AppContext* ctx;
    gchar* peer_id;
} IceCallbackData;
static void
send_ice_candidate_message(GstElement* webrtc,
    guint mlineindex,
    gchar* candidate,
    gpointer user_data)
{
    IceCallbackData* data = (IceCallbackData*)user_data;
    AppContext* ctx = data->ctx;
    const gchar* peer_id = data->peer_id;

    if (!ctx || !ctx->ws_conn ||
        soup_websocket_connection_get_state(ctx->ws_conn) != SOUP_WEBSOCKET_STATE_OPEN) {
        gst_printerr("ICE send failed: no valid websocket connection\n");
        return;
    }

    if (ctx->app_state < ROOM_CALL_OFFERING) {
        cleanup_and_quit_loop(ctx, "Can't send ICE, not in call", APP_STATE_ERROR);
        return;
    }

    JsonObject* ice = json_object_new();
    json_object_set_string_member(ice, "candidate", candidate);
    json_object_set_int_member(ice, "sdpMLineIndex", mlineindex);

    JsonObject* msg = json_object_new();
    json_object_set_object_member(msg, "ice", ice);

    gchar* text = get_string_from_json_object(msg);
    json_object_unref(msg);

    send_room_peer_msg(ctx, text, peer_id);
    g_free(text);
}


static void
send_room_peer_sdp(AppContext* ctx, GstWebRTCSessionDescription* desc, const gchar* peer_id)
{
    JsonObject* msg, * sdp;
    gchar* text, * sdptype, * sdptext;

    g_assert_cmpint(ctx->app_state, >= , ROOM_CALL_OFFERING);

    if (desc->type == GST_WEBRTC_SDP_TYPE_OFFER)
        sdptype = (gchar*) "offer";
    else if (desc->type == GST_WEBRTC_SDP_TYPE_ANSWER)
        sdptype = (gchar*)"answer";
    else
        g_assert_not_reached();

    text = gst_sdp_message_as_text(desc->sdp);
    gst_print("Sending sdp %s to %s:\n%s\n", sdptype, peer_id, text);

    sdp = json_object_new();
    json_object_set_string_member(sdp, "type", sdptype);
    json_object_set_string_member(sdp, "sdp", text);
    g_free(text);

    msg = json_object_new();
    json_object_set_object_member(msg, "sdp", sdp);
    sdptext = get_string_from_json_object(msg);
    json_object_unref(msg);

    send_room_peer_msg(ctx, sdptext, peer_id);
    g_free(sdptext);
}
typedef struct {
    AppContext* ctx;
    gchar* peer_id;
} OfferCallbackData;
/* Offer created by our pipeline, to be sent to the peer */
static void
on_offer_created(GstPromise* promise, gpointer user_data)
{
    OfferCallbackData* data = (OfferCallbackData*)user_data;
    AppContext* ctx = data->ctx;
    const gchar* peer_id = data->peer_id;

    GstElement* webrtc;
    GstWebRTCSessionDescription* offer;
    const GstStructure* reply;

    g_assert_cmpint(ctx->app_state, == , ROOM_CALL_OFFERING);

    g_assert_cmpint(gst_promise_wait(promise), == , GST_PROMISE_RESULT_REPLIED);
    reply = gst_promise_get_reply(promise);
    gst_structure_get(reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
    gst_promise_unref(promise);

    promise = gst_promise_new();
    webrtc = gst_bin_get_by_name(GST_BIN(ctx->pipeline), peer_id);
    g_assert_nonnull(webrtc);
    g_signal_emit_by_name(webrtc, "set-local-description", offer, promise);
    gst_promise_interrupt(promise);
    gst_promise_unref(promise);

    send_room_peer_sdp(ctx, offer, peer_id);
    gst_webrtc_session_description_free(offer);

    // 清除 callback data
    g_free(data->peer_id);
    g_free(data);
}

typedef struct {
    AppContext* ctx;
    gchar* peer_id;
} NegotiationCallbackData;

static void
on_negotiation_needed(GstElement* webrtc, gpointer user_data)
{
    NegotiationCallbackData* data = (NegotiationCallbackData*)user_data;
    AppContext* ctx = data->ctx;
    const gchar* peer_id = data->peer_id;

    gst_printerr("run on_negotiation_needed!");
    ctx->app_state = ROOM_CALL_OFFERING;

    OfferCallbackData* cb_data = g_new0(OfferCallbackData, 1);
    cb_data->ctx = ctx;
    cb_data->peer_id = g_strdup(peer_id);

    GstPromise* promise = gst_promise_new_with_change_func(
        (GstPromiseChangeFunc)on_offer_created, cb_data, NULL);

    g_signal_emit_by_name(webrtc, "create-offer", NULL, promise);

    // 清掉 negotiation 的 cb data（一次性）
    g_free(data->peer_id);
    g_free(data);
}



// Enable DataChannel for text/binary messages (optional)
static void
data_channel_on_open(GObject* dc, gpointer user_data)
{
    gst_print("DataChannel opened\n");
}

static void
data_channel_on_message(GObject* dc, gchar* str_data, gpointer user_data)
{
    gst_print("DataChannel message: %s\n", str_data);
}

static void
on_data_channel(GstElement* webrtc, GObject* data_channel, gpointer user_data)
{
    g_signal_connect(data_channel, "on-open", G_CALLBACK(data_channel_on_open), NULL);
    g_signal_connect(data_channel, "on-message-string", G_CALLBACK(data_channel_on_message), NULL);
}

static void
remove_peer_from_pipeline(AppContext* ctx, const gchar* peer_id)
{
    gchar* name;
    GstElement* webrtc, * vqueue, * aqueue;
    GstPad* vsrcpad, * asrcpad;

    webrtc = gst_bin_get_by_name(GST_BIN(ctx->pipeline), peer_id);
    if (!webrtc)
        return;
    gst_bin_remove(GST_BIN(ctx->pipeline), webrtc);
    gst_object_unref(webrtc);

    name = g_strdup_printf("vqueue-%s", peer_id);
    vqueue = gst_bin_get_by_name(GST_BIN(ctx->pipeline), name);
    g_free(name);
    if (vqueue) {
        GstPad* sinkpad = gst_element_get_static_pad(vqueue, "sink");
        vsrcpad = gst_pad_get_peer(sinkpad);
        gst_object_unref(sinkpad);

        GstElement* videotee = gst_bin_get_by_name(GST_BIN(ctx->pipeline), "videotee");
        if (videotee && vsrcpad) {
            gst_element_release_request_pad(videotee, vsrcpad);
            gst_object_unref(vsrcpad);
            gst_object_unref(videotee);
        }

        gst_bin_remove(GST_BIN(ctx->pipeline), vqueue);
        gst_object_unref(vqueue);
    }

    name = g_strdup_printf("aqueue-%s", peer_id);
    aqueue = gst_bin_get_by_name(GST_BIN(ctx->pipeline), name);
    g_free(name);
    if (aqueue) {
        GstPad* sinkpad = gst_element_get_static_pad(aqueue, "sink");
        asrcpad = gst_pad_get_peer(sinkpad);
        gst_object_unref(sinkpad);

        GstElement* audiotee = gst_bin_get_by_name(GST_BIN(ctx->pipeline), "audiotee");
        if (audiotee && asrcpad) {
            gst_element_release_request_pad(audiotee, asrcpad);
            gst_object_unref(asrcpad);
            gst_object_unref(audiotee);
        }

        gst_bin_remove(GST_BIN(ctx->pipeline), aqueue);
        gst_object_unref(aqueue);
    }
}



static void
add_peer_to_pipeline(AppContext* ctx, const gchar* peer_id, gboolean offer)
{
    gchar* name;
    GstElement* webrtc, * vqueue, * aqueue;
    GstPad* vsrcpad, * asrcpad, * vsinkpad, * asinkpad;
    GstElement* videotee, * audiotee;

    name = g_strdup_printf("vqueue-%s", peer_id);
    vqueue = gst_element_factory_make("queue", name);
    g_free(name);

    name = g_strdup_printf("aqueue-%s", peer_id);
    aqueue = gst_element_factory_make("queue", name);
    g_free(name);
    webrtc = gst_element_factory_make("webrtcbin", peer_id);
    g_object_set(webrtc,
        "stun-server", "stun://rtc.o3o.tw",
        "turn-server", "turn://mirdc1:mirdc1@rtc.o3o.tw",
        NULL);

    gst_bin_add_many(GST_BIN(ctx->pipeline), vqueue, aqueue, webrtc, NULL);

    // Link video
    videotee = gst_bin_get_by_name(GST_BIN(ctx->pipeline), "videotee");
    g_assert_nonnull(videotee);
    vsrcpad = gst_element_request_pad_simple(videotee, "src_%u");
    gst_object_unref(videotee);

    vsinkpad = gst_element_get_static_pad(vqueue, "sink");
    gst_pad_link(vsrcpad, vsinkpad);
    gst_object_unref(vsrcpad);
    gst_object_unref(vsinkpad);

    GstPad* vsrclink = gst_element_get_static_pad(vqueue, "src");
    GstPad* vsinklink = gst_element_request_pad_simple(webrtc, "sink_%u");
    gst_pad_link(vsrclink, vsinklink);
    gst_object_unref(vsrclink);
    gst_object_unref(vsinklink);

    // Link audio
    audiotee = gst_bin_get_by_name(GST_BIN(ctx->pipeline), "audiotee");
    g_assert_nonnull(audiotee);
    asrcpad = gst_element_request_pad_simple(audiotee, "src_%u");
    gst_object_unref(audiotee);

    asinkpad = gst_element_get_static_pad(aqueue, "sink");
    gst_pad_link(asrcpad, asinkpad);
    gst_object_unref(asrcpad);
    gst_object_unref(asinkpad);

    GstPad* asrclink = gst_element_get_static_pad(aqueue, "src");
    GstPad* asinklink = gst_element_request_pad_simple(webrtc, "sink_%u");
    gst_pad_link(asrclink, asinklink);
    gst_object_unref(asrclink);
    gst_object_unref(asinklink);

    // Signal connections
    if (offer) {
        NegotiationCallbackData* cb_data = g_new0(NegotiationCallbackData, 1);
        cb_data->ctx = ctx;
        cb_data->peer_id = g_strdup(peer_id);

        g_signal_connect(webrtc, "on-negotiation-needed",
            G_CALLBACK(on_negotiation_needed), cb_data);
    }

    IceCallbackData* cb_data = g_new0(IceCallbackData, 1);
    cb_data->ctx = ctx;
    cb_data->peer_id = g_strdup(peer_id);

    g_signal_connect(webrtc, "on-ice-candidate",
        G_CALLBACK(send_ice_candidate_message), cb_data);
    //g_signal_connect(webrtc, "pad-added", G_CALLBACK(on_incoming_stream), ctx->pipeline);
    //g_signal_connect(webrtc, "on-data-channel", G_CALLBACK(on_data_channel), NULL);

    gst_element_sync_state_with_parent(vqueue);
    gst_element_sync_state_with_parent(aqueue);
    gst_element_sync_state_with_parent(webrtc);
}


static void
call_peer(AppContext* ctx, const gchar* peer_id)
{
    add_peer_to_pipeline(ctx, peer_id, TRUE);
}

static void
incoming_call_from_peer(AppContext* ctx, const gchar* peer_id)
{
    add_peer_to_pipeline(ctx, peer_id, FALSE);
}

#define STR(x) #x
#define RTP_CAPS_OPUS(x) "application/x-rtp,media=audio,encoding-name=OPUS,payload=" STR(x)
#define RTP_CAPS_VP8(x) "application/x-rtp,media=video,encoding-name=VP8,payload=" STR(x)
#define RTP_CAPS_H264(x) "application/x-rtp,media=video,encoding-name=H264,payload=" STR(x)
static gboolean
start_pipeline(AppContext* ctx)
{
    GstStateChangeReturn ret;
    GError* error = NULL;

    ctx->pipeline = gst_parse_launch(
        "tee name=videotee ! queue ! fakesink "
        "tee name=audiotee ! queue ! fakesink "
        // Video
        "videotestsrc is-live=true pattern=ball ! videoconvert ! queue ! "
        "vp8enc deadline=1 keyframe-max-dist=2000 ! "
        "rtpvp8pay picture-id-mode=15-bit ! queue ! application/x-rtp,media=video,encoding-name=VP8,payload=96 ! videotee. "
        // Audio
        "audiotestsrc is-live=true wave=red-noise ! audioconvert ! audioresample ! queue ! "
        "opusenc ! rtpopuspay ! queue ! application/x-rtp,media=audio,encoding-name=OPUS,payload=97 ! audiotee.",
        &error
    );

    if (error) {
        gst_printerr("Failed to parse launch: %s\n", error->message);
        g_error_free(error);
        goto err;
    }

    gst_print("Starting pipeline, not transmitting yet\n");

    ret = gst_element_set_state(GST_ELEMENT(ctx->pipeline), GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
        goto err;

    return TRUE;

err:
    gst_print("Pipeline state change failure\n");
    if (ctx->pipeline)
        g_clear_object(&ctx->pipeline);
    return FALSE;
}


static gboolean
join_room_on_server(AppContext* ctx)
{
    if (soup_websocket_connection_get_state(ctx->ws_conn) != SOUP_WEBSOCKET_STATE_OPEN)
        return FALSE;

    if (!ctx->room_id)
        return FALSE;

    gst_print("Joining room %s\n", ctx->room_id);
    ctx->app_state = ROOM_JOINING;

    gchar* msg = g_strdup_printf("ROOM %s", ctx->room_id);
    soup_websocket_connection_send_text(ctx->ws_conn, msg);
    g_free(msg);

    return TRUE;
}


static gboolean
register_with_server(AppContext* ctx)
{
    gchar* hello;

    if (soup_websocket_connection_get_state(ctx->ws_conn) != SOUP_WEBSOCKET_STATE_OPEN)
        return FALSE;

    gst_print("Registering id %s with server\n", ctx->local_id);
    ctx->app_state = SERVER_REGISTERING;

    hello = g_strdup_printf("HELLO %s", ctx->local_id);
    soup_websocket_connection_send_text(ctx->ws_conn, hello);
    g_free(hello);

    return TRUE;
}


static void
on_server_closed(SoupWebsocketConnection* conn G_GNUC_UNUSED,
    gpointer user_data)
{
    AppContext* ctx = (AppContext*)user_data;

    ctx->app_state = SERVER_CLOSED;
    cleanup_and_quit_loop(ctx, "Server connection closed", (AppState)0);
}


static gboolean
do_registration(AppContext* ctx)
{
    if (ctx->app_state != SERVER_REGISTERING) {
        cleanup_and_quit_loop(ctx, "ERROR: Received HELLO when not registering", APP_STATE_ERROR);
        return FALSE;
    }

    ctx->app_state = SERVER_REGISTERED;
    gst_print("Registered with server\n");

    if (!join_room_on_server(ctx)) {
        cleanup_and_quit_loop(ctx, "ERROR: Failed to join room", ROOM_CALL_ERROR);
        return FALSE;
    }

    return TRUE;
}


/*
 * When we join a room, we are responsible for calling by starting negotiation
 * with each peer in it by sending an SDP offer and ICE candidates.
 */
static void
do_join_room(AppContext* ctx, const gchar* text)
{
    gint ii, len;
    gchar** peer_ids;

    if (ctx->app_state != ROOM_JOINING) {
        cleanup_and_quit_loop(ctx, "ERROR: Received ROOM_OK when not calling", ROOM_JOIN_ERROR);
        return;
    }

    ctx->app_state = ROOM_JOINED;
    gst_print("Room joined\n");

    if (!start_pipeline(ctx)) {
        cleanup_and_quit_loop(ctx, "ERROR: Failed to start pipeline", ROOM_CALL_ERROR);
        return;
    }

    find_peer_send_offer(ctx, ctx->local_id); // 你可能要也讓 find_peer_send_offer(ctx, ...) 吃 context

    peer_ids = g_strsplit(text, " ", -1);
    g_assert_cmpstr(peer_ids[0], == , "ROOM_OK");
    len = g_strv_length(peer_ids);

    if (len > 1 && strlen(peer_ids[1]) > 0) {
        gst_print("Found %i peers already in room\n", len - 1);
        g_print("update send_offer list: \n");
        display_list(ctx->send_offer);

        ctx->app_state = ROOM_CALL_OFFERING;
        for (ii = 1; ii < len; ii++) {
            gchar* peer_id = g_strdup(peer_ids[ii]);
            gst_print("ready send to  %s  offer!!!!!\n", peer_id);
            call_peer(ctx, peer_id);
            ctx->peers = g_list_prepend(ctx->peers, peer_id);
        }
    }

    g_strfreev(peer_ids);
}


static void
handle_error_message(AppContext* ctx, const gchar* msg)
{
    switch (ctx->app_state) {
    case SERVER_CONNECTING:
        ctx->app_state = SERVER_CONNECTION_ERROR;
        break;
    case SERVER_REGISTERING:
        ctx->app_state = SERVER_REGISTRATION_ERROR;
        break;
    case ROOM_JOINING:
        ctx->app_state = ROOM_JOIN_ERROR;
        break;
    case ROOM_JOINED:
    case ROOM_CALL_NEGOTIATING:
    case ROOM_CALL_OFFERING:
    case ROOM_CALL_ANSWERING:
        ctx->app_state = ROOM_CALL_ERROR;
        break;
    case ROOM_CALL_STARTED:
    case ROOM_CALL_STOPPING:
    case ROOM_CALL_STOPPED:
        ctx->app_state = ROOM_CALL_ERROR;
        break;
    default:
        ctx->app_state = APP_STATE_ERROR;
    }

    cleanup_and_quit_loop(ctx, msg, (AppState)0);
}
typedef struct {
    AppContext* ctx;
    gchar* peer_id;
} AnswerCallbackData;
static void
on_answer_created(GstPromise* promise, gpointer user_data)
{
    AnswerCallbackData* data = (AnswerCallbackData*)user_data;
    AppContext* ctx = data->ctx;
    const gchar* peer_id = data->peer_id;

    GstElement* webrtc;
    GstWebRTCSessionDescription* answer;
    const GstStructure* reply;

    g_assert_cmpint(ctx->app_state, == , ROOM_CALL_ANSWERING);

    g_assert_cmpint(gst_promise_wait(promise), == , GST_PROMISE_RESULT_REPLIED);
    reply = gst_promise_get_reply(promise);
    gst_structure_get(reply, "answer",
        GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, NULL);
    gst_promise_unref(promise);

    promise = gst_promise_new();
    webrtc = gst_bin_get_by_name(GST_BIN(ctx->pipeline), peer_id);
    g_assert_nonnull(webrtc);
    g_signal_emit_by_name(webrtc, "set-local-description", answer, promise);
    gst_promise_interrupt(promise);
    gst_promise_unref(promise);

    send_room_peer_sdp(ctx, answer, peer_id);
    gst_webrtc_session_description_free(answer);

    ctx->app_state = ROOM_CALL_STARTED;

    g_free(data->peer_id);
    g_free(data);
}


static void
handle_sdp_offer(AppContext* ctx, const gchar* peer_id, const gchar* text)
{
    int ret;
    GstPromise* promise;
    GstElement* webrtc;
    GstSDPMessage* sdp;
    GstWebRTCSessionDescription* offer;

    g_assert_cmpint(ctx->app_state, == , ROOM_CALL_ANSWERING);

    gst_print("Received offer:\n%s\n", text);

    ret = gst_sdp_message_new(&sdp);
    g_assert_cmpint(ret, == , GST_SDP_OK);

    ret = gst_sdp_message_parse_buffer((guint8*)text, strlen(text), sdp);
    g_assert_cmpint(ret, == , GST_SDP_OK);

    offer = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp);
    g_assert_nonnull(offer);

    promise = gst_promise_new();
    webrtc = gst_bin_get_by_name(GST_BIN(ctx->pipeline), peer_id);
    g_assert_nonnull(webrtc);
    g_signal_emit_by_name(webrtc, "set-remote-description", offer, promise);
    gst_promise_interrupt(promise);
    gst_promise_unref(promise);

    promise = gst_promise_new_with_change_func(
        (GstPromiseChangeFunc)on_answer_created, (gpointer)peer_id, NULL);
    g_signal_emit_by_name(webrtc, "create-answer", NULL, promise);

    gst_webrtc_session_description_free(offer);
    gst_object_unref(webrtc);
}


static void
handle_sdp_answer(AppContext* ctx, const gchar* peer_id, const gchar* text)
{
    int ret;
    GstPromise* promise;
    GstElement* webrtc;
    GstSDPMessage* sdp;
    GstWebRTCSessionDescription* answer;

    g_assert_cmpint(ctx->app_state, >= , ROOM_CALL_OFFERING);

    gst_print("Received answer:\n%s\n", text);

    ret = gst_sdp_message_new(&sdp);
    g_assert_cmpint(ret, == , GST_SDP_OK);

    ret = gst_sdp_message_parse_buffer((guint8*)text, strlen(text), sdp);
    g_assert_cmpint(ret, == , GST_SDP_OK);

    answer = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_ANSWER, sdp);
    g_assert_nonnull(answer);

    promise = gst_promise_new();
    webrtc = gst_bin_get_by_name(GST_BIN(ctx->pipeline), peer_id);
    g_assert_nonnull(webrtc);
    g_signal_emit_by_name(webrtc, "set-remote-description", answer, promise);
    gst_object_unref(webrtc);

    gst_promise_interrupt(promise);
    gst_promise_unref(promise);

    // 不要忘了：根據你 handle_peer_message 的邏輯
    ctx->app_state = ROOM_CALL_STARTED;
}

static gboolean
handle_peer_message(AppContext* ctx, const gchar* peer_id, const gchar* msg)
{
    JsonNode* root;
    JsonObject* object, * child;
    JsonParser* parser = json_parser_new();

    if (!json_parser_load_from_data(parser, msg, -1, NULL)) {
        gst_printerr("Unknown message '%s' from '%s', ignoring", msg, peer_id);
        g_object_unref(parser);
        return FALSE;
    }

    root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root)) {
        gst_printerr("Unknown json message '%s' from '%s', ignoring", msg, peer_id);
        g_object_unref(parser);
        return FALSE;
    }

    gst_print("Message from peer %s: %s\n", peer_id, msg);

    object = json_node_get_object(root);

    if (json_object_has_member(object, "sdp")) {
        const gchar* text, * sdp_type;

        g_assert_cmpint(ctx->app_state, >= , ROOM_JOINED);

        child = json_object_get_object_member(object, "sdp");

        if (!json_object_has_member(child, "type")) {
            cleanup_and_quit_loop(ctx, "ERROR: received SDP without 'type'", ROOM_CALL_ERROR);
            g_object_unref(parser);
            return FALSE;
        }

        sdp_type = json_object_get_string_member(child, "type");
        text = json_object_get_string_member(child, "sdp");

        if (g_strcmp0(sdp_type, "offer") == 0) {
            ctx->app_state = ROOM_CALL_ANSWERING;
            incoming_call_from_peer(ctx, peer_id);
            handle_sdp_offer(ctx, peer_id, text);
        }
        else if (g_strcmp0(sdp_type, "answer") == 0) {
            g_assert_cmpint(ctx->app_state, >= , ROOM_CALL_OFFERING);
            handle_sdp_answer(ctx, peer_id, text);
            ctx->app_state = ROOM_CALL_STARTED;
        }
        else {
            cleanup_and_quit_loop(ctx, "ERROR: invalid sdp_type", ROOM_CALL_ERROR);
            g_object_unref(parser);
            return FALSE;
        }
    }
    else if (json_object_has_member(object, "ice")) {
        GstElement* webrtc;
        const gchar* candidate;
        gint sdpmlineindex;

        child = json_object_get_object_member(object, "ice");
        candidate = json_object_get_string_member(child, "candidate");
        sdpmlineindex = json_object_get_int_member(child, "sdpMLineIndex");

        webrtc = gst_bin_get_by_name(GST_BIN(ctx->pipeline), peer_id);
        g_assert_nonnull(webrtc);
        g_signal_emit_by_name(webrtc, "add-ice-candidate", sdpmlineindex, candidate);
        gst_object_unref(webrtc);
    }
    else {
        gst_printerr("Ignoring unknown JSON message:\n%s\n", msg);
    }

    g_object_unref(parser);
    return TRUE;
}


/* One mega message handler for our asynchronous calling mechanism */
static void
on_server_message(SoupWebsocketConnection* conn, SoupWebsocketDataType type,
    GBytes* message, gpointer user_data)
{
    AppContext* ctx = (AppContext*)user_data;
    gchar* text;

    switch (type) {
    case SOUP_WEBSOCKET_DATA_BINARY:
        gst_printerr("Received unknown binary message, ignoring\n");
        return;
    case SOUP_WEBSOCKET_DATA_TEXT: {
        gsize size;
        const gchar* data = (gchar*)g_bytes_get_data(message, &size);
        /* Convert to NULL-terminated string */
        text = g_strndup(data, size);
        break;
    }
    default:
        g_assert_not_reached();
    }

    /* Server has accepted our registration, we are ready to send commands */
    if (g_strcmp0(text, "HELLO") == 0) {
        /* May fail asynchronously */
        do_registration(ctx);
        /* Room-related message */
    }
    else if (g_str_has_prefix(text, "ROOM_")) {
        /* Room joined, now we can start negotiation */
        if (g_str_has_prefix(text, "ROOM_OK ")) {
            /* May fail asynchronously */
            do_join_room(ctx, text);
        }
        else if (g_str_has_prefix(text, "ROOM_PEER")) {
            gchar** splitm = NULL;
            const gchar* peer_id;
            /* SDP and ICE, usually */
            if (g_str_has_prefix(text, "ROOM_PEER_MSG")) {
                splitm = g_strsplit(text, " ", 3);
                peer_id = (gchar*)find_peer_from_list(ctx->peers, splitm[1]);
                g_assert_nonnull(peer_id);
                /* Could be an offer or an answer, or ICE, or an arbitrary message */
                handle_peer_message(ctx, peer_id, splitm[2]);
            }
            else if (g_str_has_prefix(text, "ROOM_PEER_JOINED")) {
                splitm = g_strsplit(text, " ", 2);

                ctx->peers = g_list_prepend(ctx->peers, g_strdup(splitm[1]));
                peer_id = (gchar*)find_peer_from_list(ctx->peers, splitm[1]);

                gst_print("Peer %s has joined the room\n", peer_id);
                ctx->app_state = ROOM_CALL_OFFERING;
                remove_peer_from_pipeline(ctx, peer_id);
                call_peer(ctx, peer_id);
            }
            else if (g_str_has_prefix(text, "ROOM_PEER_LEFT")) {
                splitm = g_strsplit(text, " ", 2);
                peer_id = (gchar*)find_peer_from_list(ctx->peers, splitm[1]);
                g_assert_nonnull(peer_id);
                ctx->peers = g_list_remove(ctx->peers, peer_id);
                gst_print("Peer %s has left the room\n", peer_id);
                remove_peer_from_pipeline(ctx, peer_id);
                g_free((gchar*)peer_id);
            }
            else {
                gst_printerr("WARNING: Ignoring unknown message %s\n", text);
            }
            g_strfreev(splitm);
        }
        else {
            goto err;
        }
        /* Handle errors */
    }
    else if (g_str_has_prefix(text, "ERROR")) {
        handle_error_message(ctx, text);
    }
    else {
        goto err;
    }

out:
    g_free(text);
    return;

err:
    {
        gchar* err_s = g_strdup_printf("ERROR: unknown message %s", text);
        cleanup_and_quit_loop(ctx, err_s, APP_STATE_ERROR);
        g_free(err_s);
        goto out;
    }
}

static void
on_server_connected(SoupSession* session, GAsyncResult* res, gpointer user_data)
{
    AppContext* ctx = (AppContext*)user_data;
    GError* error = NULL;

    SoupWebsocketConnection* conn = soup_session_websocket_connect_finish(session, res, &error);
    if (error || conn == NULL) {
        if (ctx->ws_conn) {
            g_object_unref(ctx->ws_conn);
            ctx->ws_conn = NULL;
        }

        cleanup_and_quit_loop(ctx,
            error ? error->message : "Failed to establish WebSocket connection",
            SERVER_CONNECTION_ERROR);

        if (error) g_error_free(error);
        return;
    }

    ctx->ws_conn = g_object_ref(conn);

    ctx->app_state = SERVER_CONNECTED;
    gst_print("Connected to signalling server\n");

    g_signal_connect(ctx->ws_conn, "closed", G_CALLBACK(on_server_closed), ctx);
    g_signal_connect(ctx->ws_conn, "message", G_CALLBACK(on_server_message), ctx);

    register_with_server(ctx);
}


/*
 * Connect to the signalling server. This is the entrypoint for everything else.
 */
static void
connect_to_websocket_server_async(AppContext* ctx)
{
    SoupLogger* logger;
    SoupMessage* message;
    SoupSession* session;
    const char* https_aliases[] = { "wss", NULL };

    session = soup_session_new_with_options(
        SOUP_SESSION_SSL_STRICT, ctx->strict_ssl,
        SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, TRUE,
        SOUP_SESSION_HTTPS_ALIASES, https_aliases, NULL
    );

    logger = soup_logger_new(SOUP_LOGGER_LOG_BODY, -1);
    soup_session_add_feature(session, SOUP_SESSION_FEATURE(logger));
    g_object_unref(logger);

    message = soup_message_new(SOUP_METHOD_GET, ctx->server_url);

    gst_print("Connecting to server...\n");

    // 改寫 callback，也要接 ctx（需要額外改 callback 本體）
    soup_session_websocket_connect_async(session, message, NULL, NULL, NULL,
        (GAsyncReadyCallback)on_server_connected, ctx);

    ctx->app_state = SERVER_CONNECTING;
}

static gboolean
check_plugins(void)
{
    int i;
    gboolean ret;
    GstRegistry* registry;
    const gchar* needed[] = { "opus", "nice", "webrtc", "dtls", "srtp",
      "rtpmanager", "audiotestsrc", NULL
    };

    registry = gst_registry_get();
    ret = TRUE;
    for (i = 0; i < g_strv_length((gchar**)needed); i++) {
        GstPlugin* plugin;
        plugin = gst_registry_find_plugin(registry, needed[i]);
        if (!plugin) {
            gst_print("Required gstreamer plugin '%s' not found\n", needed[i]);
            ret = FALSE;
            continue;
        }
        gst_object_unref(plugin);
    }
    return ret;
}
int main(int argc, char* argv[])
{
    AppContext ctx = { 0 }; // 初始化所有欄位為 NULL 或 0
    GOptionContext* opt_context;
    GError* error = NULL;

    ctx.share_mode = default_share_mode;  // 原本的 default_share_mode 是 4
    ctx.udp_port = 5000;
    ctx.strict_ssl = FALSE;

    ctx.pipeline = pipeline;
    // 修改 GOptionEntry 結構，使其指向 ctx 成員
    static GOptionEntry entries[] = {
        { "name", 0, 0, G_OPTION_ARG_STRING, &ctx.local_id, "Name we will send to the server", "ID" },
        { "room-id", 0, 0, G_OPTION_ARG_STRING, &ctx.room_id, "Room name to join or create", "ID" },
        { "server", 0, 0, G_OPTION_ARG_STRING, &ctx.server_url, "Signalling server to connect to", "URL" },
        { "udp-port", 0, 0, G_OPTION_ARG_INT, &ctx.udp_port, "Broadcast the input streaming to specific port", "PORT" },
        { "share_mode", 0, 0, G_OPTION_ARG_INT, &ctx.share_mode, "Broadcast the input streaming to specific share_mode", "share_mode" },
        { NULL }
    };

    opt_context = g_option_context_new("- gstreamer webrtc sendrecv demo");
    g_option_context_add_main_entries(opt_context, entries, NULL);
    g_option_context_add_group(opt_context, gst_init_get_option_group());

    if (!g_option_context_parse(opt_context, &argc, &argv, &error)) {
        gst_printerr("Error initializing: %s\n", error->message);
        return -1;
    }

    if (!check_plugins())
        return -1;

    if (!ctx.room_id) {
        gst_printerr("--room-id is a required argument\n");
        return -1;
    }

    if (!ctx.local_id) {
        ctx.local_id = g_strdup_printf("%s-%i", g_get_user_name(), g_random_int_range(10, 10000));
    }
    g_strdelimit(ctx.local_id, " \t\n\r", '-');
    gst_print("Our local id is %s\n", ctx.local_id);

    if (!ctx.server_url)
        ctx.server_url = g_strdup(default_server_url);

    // 判斷是否是 localhost，自動關閉 strict_ssl
    {
        GstUri* uri = gst_uri_from_string(ctx.server_url);
        if (g_strcmp0("localhost", gst_uri_get_host(uri)) == 0 ||
            g_strcmp0("127.0.0.1", gst_uri_get_host(uri)) == 0)
        {
            ctx.strict_ssl = FALSE;
        }
        gst_uri_unref(uri);
    }

    ctx.loop = g_main_loop_new(NULL, FALSE);
    connect_to_websocket_server_async(&ctx);

    g_main_loop_run(ctx.loop);

    gst_element_set_state(GST_ELEMENT(ctx.pipeline), GST_STATE_NULL);
    gst_print("Pipeline stopped\n");

    gst_object_unref(ctx.pipeline);
    g_free(ctx.server_url);
    g_free(ctx.local_id);
    g_free(ctx.room_id);

    return 0;
}

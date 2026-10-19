/* SPDX-License-Identifier: MIT
 * ABI 4.0 wrappers. App sources keep the 3.x function names; only these
 * six aster_net_* imports cross into the firmware.
 */
#include <aster/aster.h>
static void net_zero(void *p, uint32_t value, uint32_t n)
{
	uint8_t *b = p;
	for (uint32_t i = 0; i < n; ++i) b[i] = (uint8_t)value;
}
static void net_copy(void *d, const void *s, uint32_t n)
{
	uint8_t *dd = d;
	const uint8_t *ss = s;
	for (uint32_t i = 0; i < n; ++i) dd[i] = ss[i];
}

static aster_string_t net_ref(const void *p, uint32_t n)
{
	aster_string_t s = {0, 0};
	if (n) s.offset = (uint32_t)(uintptr_t)p;
	s.length = n;
	return s;
}

int32_t aster_network_get_info(aster_network_info_t *info, uint32_t size)
{
	return aster_net_info((aster_net_info_t *)info, size);
}

int32_t aster_network_open(uint32_t kind, const char *host, uint32_t host_len,
	uint32_t port, uint32_t timeout_ms)
{
	aster_net_open_hdr_t req;
	net_zero(&req, 0, sizeof(req));
	req.size = sizeof(req);
	req.version = ASTER_NET_ABI_VERSION;
	req.proto = kind;
	req.timeout_ms = timeout_ms;
	req.port = port;
	req.target = net_ref(host, host_len);
	return aster_net_open(&req, sizeof(req));
}

int32_t aster_network_send(uint32_t handle, const void *data, uint32_t length,
	const char *host, uint32_t host_len, uint32_t port)
{
	aster_net_io_t io;
	net_zero(&io, 0, sizeof(io));
	io.size = sizeof(io);
	io.version = ASTER_NET_ABI_VERSION;
	io.port = port;
	io.name = net_ref(host, host_len);
	io.data = net_ref(data, length);
	return aster_net_io(handle, ASTER_NET_OP_SEND, &io, sizeof(io));
}

int32_t aster_network_recv(uint32_t handle, void *data, uint32_t capacity,
	aster_network_packet_t *packet, uint32_t size)
{
	aster_net_io_t io;
	aster_network_packet_t out;
	int32_t rc;
	if (!packet || size < sizeof(*packet)) return ASTER_EINVAL;
	net_zero(&io, 0, sizeof(io));
	io.size = sizeof(io);
	io.version = ASTER_NET_ABI_VERSION;
	io.data = net_ref(data, capacity);
	rc = aster_net_io(handle, ASTER_NET_OP_RECV, &io, sizeof(io));
	net_zero(&out, 0, sizeof(out));
	out.size = sizeof(out);
	out.version = 1;
	out.length = io.required;
	out.port = io.port;
	net_copy(out.ipv4, io.ipv4, sizeof(out.ipv4));
	net_copy(packet, &out, sizeof(out));
	return rc;
}

int32_t aster_network_status(uint32_t handle, aster_network_status_t *status, uint32_t size)
{
	uint8_t raw[sizeof(aster_net_status_hdr_t) + sizeof(aster_net_tcp_status_t)];
	aster_net_status_hdr_t *h = (aster_net_status_hdr_t *)raw;
	aster_net_tcp_status_t *t = (aster_net_tcp_status_t *)(raw + sizeof(*h));
	aster_network_status_t out;
	int32_t rc;
	if (!status || size < sizeof(*status)) return ASTER_EINVAL;
	rc = aster_net_status(handle, h, sizeof(raw));
	if (rc) return rc;
	net_zero(&out, 0, sizeof(out));
	out.size = sizeof(out);
	out.version = 1;
	out.kind = t->kind;
	out.state = h->proto_state;
	out.phase = t->phase;
	out.error = h->error;
	out.tx_queued = h->tx_queued;
	out.rx_queued = h->rx_queued;
	out.dropped = t->dropped;
	out.local_port = t->local_port;
	out.tx_bytes = t->tx_bytes;
	out.rx_bytes = t->rx_bytes;
	net_copy(status, &out, sizeof(out));
	return ASTER_OK;
}

int32_t aster_network_cancel(uint32_t handle) { return aster_net_cancel(handle); }
int32_t aster_network_close(uint32_t handle) { return aster_net_close(handle); }

int32_t aster_http_request(uint32_t method, const char *url, uint32_t url_length,
	const char *headers, uint32_t headers_length, const void *body,
	uint32_t body_length, uint32_t timeout_ms, uint32_t body_limit)
{
	struct { aster_net_open_hdr_t h; aster_net_http_open_t t; } req;
	net_zero(&req, 0, sizeof(req));
	req.h.size = sizeof(req);
	req.h.version = ASTER_NET_ABI_VERSION;
	req.h.proto = ASTER_NET_PROTO_HTTP;
	req.h.timeout_ms = timeout_ms;
	req.h.target = net_ref(url, url_length);
	req.t.method = method;
	req.t.body_limit = body_limit;
	req.t.headers = net_ref(headers, headers_length);
	req.t.body = net_ref(body, body_length);
	return aster_net_open(&req.h, sizeof(req));
}

int32_t aster_http_status(uint32_t handle, aster_http_status_t *status, uint32_t size)
{
	uint8_t raw[sizeof(aster_net_status_hdr_t) + sizeof(aster_net_http_status_t)];
	aster_net_status_hdr_t *h = (aster_net_status_hdr_t *)raw;
	aster_net_http_status_t *t = (aster_net_http_status_t *)(raw + sizeof(*h));
	aster_http_status_t out;
	int32_t rc;
	if (!status || size < sizeof(*status)) return ASTER_EINVAL;
	rc = aster_net_status(handle, h, sizeof(raw));
	if (rc) return rc;
	net_zero(&out, 0, sizeof(out));
	out.size = sizeof(out);
	out.version = 1;
	out.state = h->proto_state;
	out.status = t->http_status;
	out.error = h->error;
	out.redirects = t->redirects;
	out.queued = h->rx_queued;
	out.flags = t->flags;
	out.certificate_flags = h->certificate_flags;
	out.received = t->received;
	out.content_length = t->content_length;
	net_copy(status, &out, sizeof(out));
	return ASTER_OK;
}

int32_t aster_http_read(uint32_t handle, void *data, uint32_t capacity)
{
	aster_net_io_t io;
	net_zero(&io, 0, sizeof(io));
	io.size = sizeof(io);
	io.version = ASTER_NET_ABI_VERSION;
	io.data = net_ref(data, capacity);
	return aster_net_io(handle, ASTER_NET_OP_RECV, &io, sizeof(io));
}

int32_t aster_http_header(uint32_t handle, const char *name, uint32_t name_length,
	uint32_t index, char *output, uint32_t capacity, uint32_t *required)
{
	aster_net_io_t io;
	int32_t rc;
	net_zero(&io, 0, sizeof(io));
	io.size = sizeof(io);
	io.version = ASTER_NET_ABI_VERSION;
	io.name = net_ref(name, name_length);
	io.data = net_ref(output, capacity);
	io.aux = index;
	rc = aster_net_io(handle, ASTER_NET_OP_HEADER, &io, sizeof(io));
	if (required && (rc >= 0 || rc == ASTER_EMSGSIZE)) *required = io.required;
	return rc;
}

int32_t aster_http_cancel(uint32_t handle) { return aster_net_cancel(handle); }
int32_t aster_http_close(uint32_t handle) { return aster_net_close(handle); }

int32_t aster_mqtt_connect(const aster_mqtt_options_t *options, uint32_t size)
{
	struct { aster_net_open_hdr_t h; aster_net_mqtt_open_t t; } req;
	if (!options || size < sizeof(*options) || options->size != sizeof(*options) || options->version != 1)
		return ASTER_EINVAL;
	net_zero(&req, 0, sizeof(req));
	req.h.size = sizeof(req);
	req.h.version = ASTER_NET_ABI_VERSION;
	req.h.proto = ASTER_NET_PROTO_MQTT;
	req.h.flags = options->flags;
	req.h.timeout_ms = options->timeout_ms;
	req.h.port = options->port;
	req.h.target = options->host;
	req.t.keepalive_s = options->keepalive_s;
	req.t.will_qos = options->will_qos;
	req.t.client_id = options->client_id;
	req.t.username = options->username;
	req.t.password = options->password;
	req.t.will_topic = options->will_topic;
	req.t.will_payload = options->will_payload;
	return aster_net_open(&req.h, sizeof(req));
}

int32_t aster_mqtt_status(uint32_t handle, aster_mqtt_status_t *status, uint32_t size)
{
	uint8_t raw[sizeof(aster_net_status_hdr_t) + sizeof(aster_net_mqtt_status_t)];
	aster_net_status_hdr_t *h = (aster_net_status_hdr_t *)raw;
	aster_net_mqtt_status_t *t = (aster_net_mqtt_status_t *)(raw + sizeof(*h));
	aster_mqtt_status_t out;
	int32_t rc;
	if (!status || size < sizeof(*status)) return ASTER_EINVAL;
	rc = aster_net_status(handle, h, sizeof(raw));
	if (rc) return rc;
	net_zero(&out, 0, sizeof(out));
	out.size = sizeof(out);
	out.version = 1;
	out.state = h->proto_state;
	out.error = h->error;
	out.reconnects = t->reconnects;
	out.queued = h->tx_queued;
	out.pending = h->rx_queued;
	out.subscriptions = t->subscriptions;
	out.received = t->received;
	out.sent = t->sent;
	out.connack = t->connack;
	out.last_operation = t->last_operation;
	out.last_operation_kind = t->last_operation_kind;
	out.operation_error = t->operation_error;
	out.certificate_flags = h->certificate_flags;
	net_copy(status, &out, sizeof(out));
	return ASTER_OK;
}

int32_t aster_mqtt_publish(uint32_t handle, const char *topic, uint32_t topic_length,
	const void *data, uint32_t length, uint32_t qos, uint32_t retain)
{
	aster_net_io_t io;
	net_zero(&io, 0, sizeof(io));
	io.size = sizeof(io);
	io.version = ASTER_NET_ABI_VERSION;
	io.flags = (qos & 3u) | ((retain & 1u) << 2);
	io.name = net_ref(topic, topic_length);
	io.data = net_ref(data, length);
	return aster_net_io(handle, ASTER_NET_OP_SEND, &io, sizeof(io));
}

int32_t aster_mqtt_subscribe(uint32_t handle, const char *filter, uint32_t length, uint32_t qos)
{
	aster_net_io_t io;
	net_zero(&io, 0, sizeof(io));
	io.size = sizeof(io);
	io.version = ASTER_NET_ABI_VERSION;
	io.flags = qos & 3u;
	io.name = net_ref(filter, length);
	return aster_net_io(handle, ASTER_NET_OP_SUBSCRIBE, &io, sizeof(io));
}

int32_t aster_mqtt_unsubscribe(uint32_t handle, const char *filter, uint32_t length)
{
	aster_net_io_t io;
	net_zero(&io, 0, sizeof(io));
	io.size = sizeof(io);
	io.version = ASTER_NET_ABI_VERSION;
	io.name = net_ref(filter, length);
	return aster_net_io(handle, ASTER_NET_OP_UNSUBSCRIBE, &io, sizeof(io));
}

int32_t aster_mqtt_operation(uint32_t handle, uint32_t sequence, int32_t *error)
{
	aster_net_io_t io;
	int32_t rc;
	if (!error) return ASTER_EINVAL;
	net_zero(&io, 0, sizeof(io));
	io.size = sizeof(io);
	io.version = ASTER_NET_ABI_VERSION;
	io.aux = sequence;
	rc = aster_net_io(handle, ASTER_NET_OP_OPERATION, &io, sizeof(io));
	if (!rc) *error = (int32_t)io.required;
	return rc;
}

int32_t aster_mqtt_recv(uint32_t handle, aster_mqtt_message_t *message, uint32_t size)
{
	aster_net_io_t io;
	int32_t rc;
	if (!message || size < sizeof(*message)) return ASTER_EINVAL;
	net_zero(&io, 0, sizeof(io));
	io.size = sizeof(io);
	io.version = ASTER_NET_ABI_VERSION;
	io.name = net_ref(message->topic, ASTER_MQTT_TOPIC_LIMIT);
	io.data = net_ref(message->data, ASTER_MQTT_PAYLOAD_LIMIT);
	rc = aster_net_io(handle, ASTER_NET_OP_RECV, &io, sizeof(io));
	if (rc) return rc;
	message->size = sizeof(*message);
	message->version = 1;
	message->length = io.required;
	message->topic_length = io.port;
	message->qos = io.flags & 3u;
	message->flags = ((io.flags & 4u) ? ASTER_MQTT_MESSAGE_RETAIN : 0) |
		((io.flags & 8u) ? ASTER_MQTT_MESSAGE_DUP : 0);
	return ASTER_OK;
}

int32_t aster_mqtt_cancel(uint32_t handle) { return aster_net_cancel(handle); }
int32_t aster_mqtt_close(uint32_t handle) { return aster_net_close(handle); }

int32_t aster_ws_connect(const char *url, uint32_t url_length, uint32_t timeout_ms)
{
	aster_net_open_hdr_t req;
	net_zero(&req, 0, sizeof(req));
	req.size = sizeof(req);
	req.version = ASTER_NET_ABI_VERSION;
	req.proto = ASTER_NET_PROTO_WS;
	req.timeout_ms = timeout_ms;
	req.target = net_ref(url, url_length);
	return aster_net_open(&req, sizeof(req));
}

int32_t aster_ws_status(uint32_t handle, aster_ws_status_t *status, uint32_t size)
{
	uint8_t raw[sizeof(aster_net_status_hdr_t) + sizeof(aster_net_ws_status_t)];
	aster_net_status_hdr_t *h = (aster_net_status_hdr_t *)raw;
	aster_net_ws_status_t *t = (aster_net_ws_status_t *)(raw + sizeof(*h));
	aster_ws_status_t out;
	int32_t rc;
	if (!status || size < sizeof(*status)) return ASTER_EINVAL;
	rc = aster_net_status(handle, h, sizeof(raw));
	if (rc) return rc;
	net_zero(&out, 0, sizeof(out));
	out.size = sizeof(out);
	out.version = 1;
	out.state = h->proto_state;
	out.error = h->error;
	out.queued = h->rx_queued;
	out.pending = h->tx_queued;
	out.received = t->received;
	out.sent = t->sent;
	out.close_code = t->close_code;
	out.certificate_flags = h->certificate_flags;
	net_copy(status, &out, sizeof(out));
	return ASTER_OK;
}

int32_t aster_ws_send(uint32_t handle, uint32_t opcode, const void *data, uint32_t length)
{
	aster_net_io_t io;
	net_zero(&io, 0, sizeof(io));
	io.size = sizeof(io);
	io.version = ASTER_NET_ABI_VERSION;
	io.flags = (opcode & 0xffu) << 8;
	io.data = net_ref(data, length);
	return aster_net_io(handle, ASTER_NET_OP_SEND, &io, sizeof(io));
}

int32_t aster_ws_recv(uint32_t handle, aster_ws_message_t *message, uint32_t size)
{
	aster_net_io_t io;
	int32_t rc;
	if (!message || size < sizeof(*message)) return ASTER_EINVAL;
	net_zero(&io, 0, sizeof(io));
	io.size = sizeof(io);
	io.version = ASTER_NET_ABI_VERSION;
	io.data = net_ref(message->data, ASTER_WS_MESSAGE_LIMIT);
	rc = aster_net_io(handle, ASTER_NET_OP_RECV, &io, sizeof(io));
	if (rc) return rc;
	message->size = sizeof(*message);
	message->version = 1;
	message->opcode = (io.flags >> 8) & 0xffu;
	message->length = io.required;
	return ASTER_OK;
}

int32_t aster_ws_cancel(uint32_t handle) { return aster_net_cancel(handle); }
int32_t aster_ws_close(uint32_t handle) { return aster_net_close(handle); }

///
//
// LibSourcey
// Copyright (c) 2005, Sourcey <https://sourcey.com>
//
// SPDX-License-Identifier: LGPL-2.1+
//
/// @addtogroup net
/// @{


#include "scy/net/sslsocket.h"
#include "scy/logger.h"
#include "scy/net/sslmanager.h"


using namespace std;


namespace scy {
namespace net {


// TODO: Using client context, should assert no bind()/listen() on this socket


SSLSocket::SSLSocket(uv::Loop* loop)
    : TCPSocket(loop)
    , _context(nullptr)
    , _session(nullptr)
    , _sslAdapter(this)
{
    // TraceS(this) << "Create" << endl;
}


SSLSocket::SSLSocket(SSLContext::Ptr context, uv::Loop* loop)
    : TCPSocket(loop)
    , _context(context)
    , _session(nullptr)
    , _sslAdapter(this)
{
    // TraceS(this) << "Create" << endl;
}


SSLSocket::SSLSocket(SSLContext::Ptr context, SSLSession::Ptr session, uv::Loop* loop)
    : TCPSocket(loop)
    , _context(context)
    , _session(session)
    , _sslAdapter(this)
{
    // TraceS(this) << "Create" << endl;
}


SSLSocket::~SSLSocket()
{
    // TraceS(this) << "Destroy" << endl;
}


int SSLSocket::available() const
{
    return _sslAdapter.available();
}


void SSLSocket::close()
{
    TCPSocket::close();
}


bool SSLSocket::shutdown()
{
    // TraceS(this) << "Shutdown" << endl;
    try {
        // Try to gracefully shutdown the SSL connection
        _sslAdapter.shutdown();
    } catch (...) {
    }
    return TCPSocket::shutdown();
}


ssize_t SSLSocket::send(const char* data, size_t len, int flags)
{
    return send(data, len, peerAddress(), flags);
}


ssize_t SSLSocket::send(const char* data, size_t len, const net::Address& /* peerAddress */, int /* flags */)
{
    // TraceS(this) << "Send: " << len << endl;
    assert(Thread::currentID() == tid());
    // assert(len <= net::MAX_TCP_PACKET_SIZE);

    if (!active()) {
        WarnL << "Send error" << endl;
        return -1;
    }

    // Send unencrypted data to the SSL context

    assert(_sslAdapter._ssl);

    _sslAdapter.addOutgoingData(data, len);
    _sslAdapter.flush();
    return len;
}


void SSLSocket::acceptConnection()
{
    assert(_context->isForServerUse());

    // Create the shared socket pointer so the if the socket handle is not
    // incremented the accepted socket will be destroyed.
    auto socket = std::make_shared<net::SSLSocket>(_context, loop());

    // TraceS(this) << "Accept SSL connection: " << socket->ptr() << endl;
    uv::invokeOrThrow("Cannot initialize SSL socket",
                  &uv_tcp_init, loop(), socket->ptr<uv_tcp_t>());
    // UVCallOrThrow("Cannot initialize SSL socket",
    //               uv_tcp_init, loop(), socket->ptr<uv_tcp_t>())

    if (uv_accept(ptr<uv_stream_t>(), socket->ptr<uv_stream_t>()) == 0) {
        socket->readStart();
        socket->_sslAdapter.initServer();

        AcceptConnection.emit(socket);
    }
    else {
        assert(0 && "uv_accept should not fail");
    }
}


void SSLSocket::useSession(SSLSession::Ptr session)
{
    _session = session;
}


SSLSession::Ptr SSLSocket::currentSession()
{
    if (_sslAdapter._ssl) {
        SSL_SESSION* session = SSL_get1_session(_sslAdapter._ssl);
        if (session) {
            if (_session && session == _session->sslSession()) {
                SSL_SESSION_free(session);
                return _session;
            } else
                return std::make_shared<SSLSession>(
                    session); // new SSLSession(session);
        }
    }
    return 0;
}


void SSLSocket::useContext(SSLContext::Ptr context)
{
    if (_sslAdapter._ssl)
        throw std::runtime_error(
            "Cannot change the SSL context for an active socket.");

    _context = context;
}


SSLContext::Ptr SSLSocket::context() const
{
    return _context;
}


bool SSLSocket::sessionWasReused()
{
    if (_sslAdapter._ssl)
        return SSL_session_reused(_sslAdapter._ssl) != 0;
    else
        return false;
}


net::TransportType SSLSocket::transport() const
{
    return net::SSLTCP;
}


//
// Callbacks

void SSLSocket::onRead(const char* data, size_t len)
{
    // TraceS(this) << "On SSL read: " << len << endl;

    // SSL encrypted data is sent to the SSL context
    _sslAdapter.addIncomingData(data, len);
    _sslAdapter.flush();
}


void SSLSocket::onConnect(uv_connect_t* handle, int status)
{
    // TraceS(this) << "On connect" << endl;
    if (status) {
        setUVError("SSL connect error", status);
        return;
    } else
        readStart();

    _sslAdapter.initClient();
    // _sslAdapter.start();

    onSocketConnect(*this);
}


} // namespace net
} // namespace scy


/// @\}

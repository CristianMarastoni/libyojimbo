/*
    Yojimbo Client/Server Network Library.
    
    Copyright © 2016, The Network Protocol Company, Inc.

    Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

        1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

        2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer 
           in the documentation and/or other materials provided with the distribution.

        3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived 
           from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, 
    INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR 
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
    WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
    USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "yojimbo_matcher.h"

#include "mbedtls/config.h"
#include "mbedtls/platform.h"
#include "mbedtls/net.h"
#include "mbedtls/debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"

#define SERVER_PORT "8080"
#define SERVER_NAME "localhost"
#define GET_REQUEST "GET /match/123141/1 HTTP/1.0\r\n\r\n"

namespace yojimbo
{
    struct MatcherImpl
    {
        mbedtls_net_context server_fd;
        mbedtls_entropy_context entropy;
        mbedtls_ctr_drbg_context ctr_drbg;
        mbedtls_ssl_context ssl;
        mbedtls_ssl_config conf;
        mbedtls_x509_crt cacert;
    };

    Matcher::Matcher()
    {
        m_initialized = false;
        m_status = MATCHER_IDLE;
        m_impl = new MatcherImpl();     // todo: convert to allocator
    }

    Matcher::~Matcher()
    {
        mbedtls_net_free( &m_impl->server_fd );

        mbedtls_x509_crt_free( &m_impl->cacert );
        mbedtls_ssl_free( &m_impl->ssl );
        mbedtls_ssl_config_free( &m_impl->conf );
        mbedtls_ctr_drbg_free( &m_impl->ctr_drbg );
        mbedtls_entropy_free( &m_impl->entropy );

        delete m_impl;
        m_impl = NULL;
    }

    bool Matcher::Initialize()
    {
        int ret;

        const char *pers = "ssl_client1";

        mbedtls_net_init( &m_impl->server_fd );
        mbedtls_ssl_init( &m_impl->ssl );
        mbedtls_ssl_config_init( &m_impl->conf );
        mbedtls_x509_crt_init( &m_impl->cacert );
        mbedtls_ctr_drbg_init( &m_impl->ctr_drbg );
        mbedtls_entropy_init( &m_impl->entropy );

        if ( ( ret = mbedtls_ctr_drbg_seed( &m_impl->ctr_drbg, mbedtls_entropy_func, &m_impl->entropy,
                                            (const unsigned char *) pers,
                                            strlen( pers ) ) ) != 0 )
        {
            printf( " failed\n  ! mbedtls_ctr_drbg_seed returned %d\n", ret );
            return false;
        }

        ret = mbedtls_x509_crt_parse( &m_impl->cacert, (const unsigned char *) mbedtls_test_cas_pem, mbedtls_test_cas_pem_len );
        if( ret < 0 )
        {
            mbedtls_printf( " failed\n  !  mbedtls_x509_crt_parse returned -0x%x\n\n", -ret );
            return false;
        }

        m_initialized = true;

        return true;
    }

    void Matcher::RequestMatch( uint32_t protocolId, uint64_t clientId )
    {
        assert( m_initialized );

        (void)protocolId;
        (void)clientId;
        
        /*
         * 1. Start the connection
         */
        mbedtls_printf( "  . Connecting to tcp/%s/%s...", SERVER_NAME, SERVER_PORT );
        fflush( stdout );

        int ret;

        if( ( ret = mbedtls_net_connect( &m_impl->server_fd, SERVER_NAME,
                                         SERVER_PORT, MBEDTLS_NET_PROTO_TCP ) ) != 0 )
        {
            mbedtls_printf( " failed\n  ! mbedtls_net_connect returned %d\n\n", ret );
            m_status = MATCHER_FAILED;
            return;
        }

        mbedtls_printf( " ok\n" );

        /*
         * 2. Setup stuff
         */
        mbedtls_printf( "  . Setting up the SSL/TLS structure..." );
        fflush( stdout );

        if( ( ret = mbedtls_ssl_config_defaults( &m_impl->conf,
                        MBEDTLS_SSL_IS_CLIENT,
                        MBEDTLS_SSL_TRANSPORT_STREAM,
                        MBEDTLS_SSL_PRESET_DEFAULT ) ) != 0 )
        {
            mbedtls_printf( " failed\n  ! mbedtls_ssl_config_defaults returned %d\n\n", ret );
            m_status = MATCHER_FAILED;
            return;
        }

        mbedtls_printf( " ok\n" );

        /* OPTIONAL is not optimal for security,
         * but makes interop easier in this simplified example */
        mbedtls_ssl_conf_authmode( &m_impl->conf, MBEDTLS_SSL_VERIFY_OPTIONAL );
        mbedtls_ssl_conf_ca_chain( &m_impl->conf, &m_impl->cacert, NULL );
        mbedtls_ssl_conf_rng( &m_impl->conf, mbedtls_ctr_drbg_random, &m_impl->ctr_drbg );
        //mbedtls_ssl_conf_dbg( &m_impl->conf, m_impl->my_debug, stdout );

        if( ( ret = mbedtls_ssl_setup( &m_impl->ssl, &m_impl->conf ) ) != 0 )
        {
            mbedtls_printf( " failed\n  ! mbedtls_ssl_setup returned %d\n\n", ret );
            m_status = MATCHER_FAILED;
            return;
        }

        if( ( ret = mbedtls_ssl_set_hostname( &m_impl->ssl, "mbed TLS Server 1" ) ) != 0 )
        {
            mbedtls_printf( " failed\n  ! mbedtls_ssl_set_hostname returned %d\n\n", ret );
            m_status = MATCHER_FAILED;
            return;
        }

        mbedtls_ssl_set_bio( &m_impl->ssl, &m_impl->server_fd, mbedtls_net_send, mbedtls_net_recv, NULL );

        /*
         * 4. Handshake
         */
        mbedtls_printf( "  . Performing the SSL/TLS handshake..." );
        fflush( stdout );

        while( ( ret = mbedtls_ssl_handshake( &m_impl->ssl ) ) != 0 )
        {
            if( ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE )
            {
                mbedtls_printf( " failed\n  ! mbedtls_ssl_handshake returned -0x%x\n\n", -ret );
                m_status = MATCHER_FAILED;
                return;
            }
        }

        mbedtls_printf( " ok\n" );

        /*
         * 5. Verify the server certificate
         */
        mbedtls_printf( "  . Verifying peer X.509 certificate..." );

        uint32_t flags;

        /* In real life, we probably want to bail out when ret != 0 */
        if( ( flags = mbedtls_ssl_get_verify_result( &m_impl->ssl ) ) != 0 )
        {
            char vrfy_buf[512];

            mbedtls_printf( " failed\n" );

            mbedtls_x509_crt_verify_info( vrfy_buf, sizeof( vrfy_buf ), "  ! ", flags );

            mbedtls_printf( "%s\n", vrfy_buf );
        }
        else
            mbedtls_printf( " ok\n" );

        /*
         * 3. Write the GET request
         */
        mbedtls_printf( "  > Write to server:" );
        fflush( stdout );

        unsigned char buf[4*1024];

        int len = sprintf( (char *) buf, GET_REQUEST );

        while( ( ret = mbedtls_ssl_write( &m_impl->ssl, buf, len ) ) <= 0 )
        {
            if( ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE )
            {
                mbedtls_printf( " failed\n  ! mbedtls_ssl_write returned %d\n\n", ret );
                m_status = MATCHER_FAILED;
                return;
            }
        }

        len = ret;
        mbedtls_printf( " %d bytes written\n\n%s", len, (char *) buf );

        /*
         * 7. Read the HTTP response
         */
        mbedtls_printf( "  < Read from server:" );
        fflush( stdout );

        do
        {
            len = sizeof( buf ) - 1;
            memset( buf, 0, sizeof( buf ) );
            ret = mbedtls_ssl_read( &m_impl->ssl, buf, len );

            if( ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE )
                continue;

            if( ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY )
                break;

            if( ret < 0 )
            {
                //mbedtls_printf( "failed\n  ! mbedtls_ssl_read returned %d\n\n", ret );
                break;
            }

            if( ret == 0 )
            {
                mbedtls_printf( "\n\nEOF\n\n" );
                break;
            }

            len = ret;
            mbedtls_printf( " %d bytes read\n\n%s\n", len, (char *) buf );
        }
        while( 1 );

        mbedtls_ssl_close_notify( &m_impl->ssl );

        m_status = MATCHER_READY;
    }

    MatcherStatus Matcher::GetStatus()
    {
        return m_status;
    }

    void Matcher::GetMatch( Match & match )
    {
        match = Match();
    }
}

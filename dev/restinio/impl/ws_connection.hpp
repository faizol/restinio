/*
	restinio
*/

/*!
	WebSocket connection routine.
*/

#pragma once

#include <asio.hpp>

#include <nodejs/http_parser/http_parser.h>

#include <fmt/format.h>

#include <restinio/exception.hpp>
#include <restinio/http_headers.hpp>
#include <restinio/connection_handle.hpp>
#include <restinio/request_handler.hpp>
#include <restinio/impl/header_helpers.hpp>
#include <restinio/impl/response_coordinator.hpp>
#include <restinio/impl/connection_settings.hpp>
#include <restinio/impl/fixed_buffer.hpp>
#include <restinio/impl/raw_resp_output_ctx.hpp>

namespace restinio
{

namespace impl
{

//
// ws_connection_t
//

//! Context for handling websocket connections.
/*
*/
template < typename TRAITS, typename WS_MESSAGE_HANDLER >
class ws_connection_t final
	:	public ws_connection_base_t
{
	public:
		using message_handler_t = WS_MESSAGE_HANDLER;
		using logger_t = typename TRAITS::logger_t;
		using strand_t = typename TRAITS::strand_t;
		using stream_socket_t = typename TRAITS::stream_socket_t;

		ws_connection_t(
			//! Connection id.
			std::uint64_t conn_id,
			//! Connection socket.
			stream_socket_t && socket,
			//! Settings that are common for connections.
			connection_settings_shared_ptr_t< TRAITS > settings,
			message_handler_t msg_handler )
			:	connection_base_t{ conn_id }
			,	m_socket{ std::move( socket ) }
			,	m_strand{ m_socket.get_executor() }
			,	m_settings{ std::move( settings ) }
			,	m_input{ m_settings->m_buffer_size }
			,	m_msg_handler{ msg_handler }
			,	m_logger{ *( m_settings->m_logger ) }
		{
			// Notify of a new connection instance.
			m_logger.trace( [&]{
					return fmt::format(
						"[ws_connection:{}] start connection with {}",
						connection_id(),
						m_socket.remote_endpoint() );
			} );
		}

		ws_connection_t( const ws_connection_t & ) = delete;
		ws_connection_t( ws_connection_t && ) = delete;
		void operator = ( const ws_connection_t & ) = delete;
		void operator = ( ws_connection_t && ) = delete;

		~ws_connection_t()
		{
			try
			{
				// Notify of a new connection instance.
				m_logger.trace( [&]{
					return fmt::format(
						"[ws_connection:{}] destroyed",
						connection_id() );
				} );
			}
			catch( ... )
			{}
		}

		virtual void
		close() override
		{
			//! Run write message on io_service loop if possible.
			asio::dispatch(
				get_executor(),
				[ this, ctx = shared_from_this() ](){
					try
					{
						graceful_close();
					}
					catch( const std::exception & ex )
					{
						m_logger.error( [&]{
							return fmt::format(
								"[ws_connection:{}] close operation error: {}",
								connection_id(),
								ex.what() );
						} );
					}
			} );
		}

		//! Write pieces of outgoing data.
		virtual void
		write_data( buffers_container_t bufs ) override
		{
			// NOTE: See connection_t::write_response_parts impl.
			auto bufs_transmit_instance =
				std::make_unique< buffers_container_t >( std::move( bufs ) );

			//! Run write message on io_service loop if possible.
			asio::dispatch(
				get_executor(),
				[ this,
					bufs = std::move( bufs_transmit_instance ),
					ctx = shared_from_this() ](){
						try
						{
							write_data_impl( std::move( *bufs ) );
						}
						catch( const std::exception & ex )
						{
							trigger_error_and_close( [&]{
								return fmt::format(
									"[ws_connection:{}] unable to write data: {}",
									connection_id(),
									ex.what() );
							} );
						}
				} );
		}

	private:
		void
		write_data_impl( buffers_container_t bufs )
		{
			assert( m_socket );

			if( !m_socket.is_open() )
			{
				m_logger.warn( [&]{
					return fmt::format(
							"[ws_connection:{}] try to write response, "
							"while socket is closed",
							connection_id() );
				} );
				return;
			}

			if( m_awaiting_buffers.empty() )
			{
				m_awaiting_buffers = std::move( bufs );
			}
			else
			{
				m_awaiting_buffers.reserve( m_awaiting_buffers.size() + bufs.size() );
				for( auto & buf : bufs )
					m_awaiting_buffers.emplace_back( std::move( buf ) );
			}

			init_write_if_necessary();
		}

		// Check if there is something to write,
		// and if so starts write operation.
		void
		init_write_if_necessary()
		{
			if( !m_resp_out_ctx.transmitting() )
			{
				if( m_resp_out_ctx.obtain_bufs( m_awaiting_buffers ) )
				{
					auto & bufs = m_resp_out_ctx.create_bufs();

					m_logger.trace( [&]{
						return fmt::format(
							"[ws_connection:{}] sending resp data, "
							"buf count: {}",
							connection_id(),
							bufs.size() ); } );

					// There is somethig to write.
					asio::async_write(
						m_socket,
						bufs,
						asio::bind_executor(
							get_executor(),
							[ this,
								ctx = shared_from_this() ]
								( auto ec, std::size_t written ){
									this->after_write(
										ec,
										written );
							} ) );

					// TODO: guard_write_operation();
				}
			}
		}

		//! Handle write response finished.
		inline void
		after_write(
			const std::error_code & ec,
			std::size_t written )
		{
			if( !ec )
			{
				// Release buffers.
				m_resp_out_ctx.done();

				m_logger.trace( [&]{
					return fmt::format(
							"[ws_connection:{}] outgoing data was sent: {}b",
							connection_id() );
				} );

				if( m_socket.is_open() )
				{
					// Start another write opertion
					// if there is something to send.
					init_write_if_necessary();
				}
			}
			else
			{
				if( ec != asio::error::operation_aborted )
				{
					trigger_error_and_close( [&]{
						return fmt::format(
							"[ws_connection:{}] unable to write: {}",
							connection_id(),
							ec.message() );
					} );
				}
				// else: Operation aborted only in case of close was called.
			}
		}

		//! Close WebSocket connection in a graceful manner
		//! sending a close-message
		void
		graceful_close()
		{
			// TODO:
			// Send close frame.

			// That will close socket and ensure that outgoing data will be sent.
			close_impl();
		}


		//! An executor for callbacks on async operations.
		inline strand_t &
		get_executor()
		{
			return m_strand;
		}

		//! Close connection functions.
		//! \{

		//! Standard close routine.
		void
		close_impl()
		{
			m_logger.trace( [&]{
				return fmt::format(
						"[ws_connection:{}] close",
						connection_id() );
			} );

			asio::error_code ignored_ec;
			m_socket.shutdown(
				asio::ip::tcp::socket::shutdown_both,
				ignored_ec );
			m_socket.close();
		}

		//! Trigger an error.
		/*!
			Closes the connection and write to log
			an error message.
		*/
		template< typename MSG_BUILDER >
		void
		trigger_error_and_close( MSG_BUILDER && msg_builder )
		{
			m_logger.error( std::move( msg_builder ) );

			close_impl();
		}
		//! \}

		//! Connection.
		stream_socket_t m_socket;

		//! Sync object for connection events.
		strand_t m_strand;

		//! Common paramaters of a connection.
		connection_settings_shared_ptr_t< TRAITS > m_settings;

		message_handler_t m_msg_handler;

		//! Input routine.
		connection_input_t m_input;

		//! Write to socket operation context.
		raw_resp_output_ctx_t m_resp_out_ctx;

		//! Output buffers queue.
		buffers_container_t m_awaiting_buffers;

		//! Logger for operation
		logger_t & m_logger;
};

} /* namespace impl */

} /* namespace restinio */

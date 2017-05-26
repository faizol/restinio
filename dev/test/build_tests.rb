MxxRu::Cpp::composite_target {

	required_prj( "test/header/prj.ut.rb" )
	required_prj( "test/default_constructed_settings/prj.ut.rb" )
	required_prj( "test/ref_qualifiers_settings/prj.ut.rb" )
	required_prj( "test/response_coordinator/prj.ut.rb" )

	required_prj( "test/handle_requests/method/prj.ut.rb" )
	required_prj( "test/handle_requests/echo_body/prj.ut.rb" )
	required_prj( "test/handle_requests/timeouts/prj.ut.rb" )
	required_prj( "test/handle_requests/slow_transmit/prj.ut.rb" )
	required_prj( "test/handle_requests/throw_exception/prj.ut.rb" )
	required_prj( "test/handle_requests/user_controlled_output/prj.ut.rb" )
	required_prj( "test/handle_requests/chunked_output/prj.ut.rb" )

	required_prj( "test/http_pipelining/sequence/prj.ut.rb" )
	required_prj( "test/http_pipelining/timeouts/prj.ut.rb" )

	required_prj( "test/router/first_match_router/prj.ut.rb" )
	required_prj( "test/router/express/prj.ut.rb" )
}

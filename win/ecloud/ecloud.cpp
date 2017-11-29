
#include "ecloud.h"

//////////////////////////////////////////////////////////////////////////

ParallelCore& global_pc( ParallelCore* pc )
{
	static ParallelCore* g_pc = nullptr ;
	if ( g_pc == nullptr && pc != nullptr )
	{
		g_pc = pc ;
	}

	return *g_pc ;
}

ParallelCore& global_pc()
{
	return global_pc( nullptr ) ;
}

ORMapper& global_db( ORMapper* db )
{
	static ORMapper* g_db = nullptr ;
	if ( g_db == nullptr && db != nullptr )
	{
		g_db = db ;
	}

	return *g_db ;
}

ORMapper& global_db()
{
	return global_db( nullptr ) ;
}

//////////////////////////////////////////////////////////////////////////

void global_update_cloud()
{
	auto task_insert_index = []( service_meta service, int64_t i )
	{
		try
		{
			auto subject = email_subject( service.pop3.c_str(), service.user.c_str(), service.pawd.c_str(), i ) ;
			
			file_meta fm ;
			meta_from_json( fm, subject.c_str() ) ;

			if ( fm.id.empty() || fm.bytes == 0 || fm.end == 0 )
			{
				return ;
			}
			
			dbmeta_cloudfile index ;
			index.from_meta( fm, service.user, i ) ;
			global_db().Insert( index ) ;
		}

		catch ( ... )
		{

		}
	} ;

	auto task_check_update = [ task_insert_index ]
	{
		try
		{
			for ( auto&& node : global_db().Query( dbmeta_cloudnode{} ).ToList() )
			{
				// check node update.
				auto service = node.to_meta() ;
				auto news = service_update( service ) ;
				if ( news == 0 )
				{
					continue ;
				}

				// update db for node information.
				node.from_meta( service ) ;
				global_db().Update( node ) ;

				// update cloudfile index.
				for ( int64_t i = service.mails - news + 1 ; i <= service.mails ; ++i )
				{
					global_pc().post( std::bind( task_insert_index, service, i ) ) ;
				}
			}
		}

		catch( ... )
		{

		}
	} ;

	global_pc().post( task_check_update ) ;
}

bool global_cloudfile_exist( const string& id )
{
	try
	{
		dbmeta_cloudfile model ;
		auto field = FieldExtractor{ model } ;

		for ( auto&& cf : global_db().Query( model ).Where( field( model.id ) == id ).ToVector() )
		{
			// single chunk file.
			if ( cf.bytes == ( cf.end - cf.beg ) )
			{
				return true ;
			}
		}
	}

	catch ( ... )
	{

	}

	return false ;
}

vector<service_meta> global_cloudnodes()
{
	vector<service_meta> ret ;

	try
	{
		dbmeta_cloudnode model ;
		for ( auto&& node : global_db().Query( model ).ToVector() )
		{
			ret.emplace_back( node.to_meta() ) ;
		}
	}

	catch ( ... )
	{

	}

	return ret ;
}

bool global_cloudfile_download( const string& id, const string& file )
{
	try
	{
		dbmeta_cloudfile model_file ;
		dbmeta_cloudnode model_node ;
		auto field = FieldExtractor{ model_file, model_node } ;

		auto result = global_db().Query( model_file )
			.Join( model_node, field( model_file.service ) == field( model_node.user ) )
			.Where( field( model_file.id ) == id ).ToList() ;
		
		if ( result.empty() )
		{
			return false ;
		}

		for ( auto&& row : result )
		{
			auto tag = std::get< 1 >( row ).Value() ;
			auto bytes = std::get< 2 >( row ).Value() ;
			auto beg = std::get< 3 >( row ).Value() ;
			auto end = std::get< 4 >( row ).Value() ;
			auto number = std::get< 6 >( row ).Value() ;

			auto user = std::get< 7 >( row ).Value() ;
			auto pawd = std::get< 8 >( row ).Value() ;
			auto pop3 = std::get< 10 >( row ).Value() ;

			// try single chunk to file.
			if ( bytes == ( end - beg ) )
			{
				if ( file_create( file.c_str(), bytes ) != 0 )
				{
					continue ;
				}

				if ( email_recv(
					pop3.c_str(),
					user.c_str(),
					pawd.c_str(),
					number,
					file.c_str(),
					0, bytes, 0 ) != 200 )
				{
					continue ;
				}

				return true ;
			}
		}
	}

	catch ( ... )
	{

	}

	return false ;
}

//////////////////////////////////////////////////////////////////////////

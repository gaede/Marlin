
#include "marlin/XMLParser.h"
#include "marlin/Exceptions.h"
#include "marlin/tinyxml.h"

#include <algorithm>

#include <sstream>
#include <set>


#include <memory>

namespace marlin{

    // open steering file with processor names 
    XMLParser::XMLParser( const std::string&  fileName, bool forCCheck ) :
        _current(NULL) , _fileName( fileName ), _forCCheck( forCCheck ) {
        }

    XMLParser::~XMLParser(){
    }

    void XMLParser::parse(){


        _doc = std::unique_ptr<TiXmlDocument>( new TiXmlDocument );
        bool loadOkay = _doc->LoadFile(_fileName  ) ;

        if( !loadOkay ) {

            std::stringstream str ;

            str  << "XMLParser::parse error in file [" << _fileName 
                << ", row: " << _doc->ErrorRow() << ", col: " << _doc->ErrorCol() << "] : "
                << _doc->ErrorDesc() ;

            throw ParseException( str.str() ) ;
        }

        //     TiXmlHandle docHandle( _doc );

        TiXmlElement* root = _doc->RootElement();
        if( root==0 || strcmp(root->Value(),"marlin") != 0 ){

            throw ParseException(std::string( "XMLParser::parse : no root tag <marlin>...</marlin> found in  ") 
                    + _fileName  ) ;
        }

        TiXmlNode* section = 0 ;


        _map[ "Global" ] = std::make_shared<StringParameters>();
        StringParameters*  globalParameters = _map[ "Global" ].get();

        section = root->FirstChild("global")  ;

        if( section != 0  ){
            _current =  _map[ "Global" ].get() ;
            parametersFromNode( section ) ;

        }  else {

            throw ParseException(std::string( "XMLParser::parse : no <global/> section found in  ") 
                    + _fileName  ) ;
        }


        // create global parameter ActiveProcessors from <execute> section
        //     TiXmlElement activeProcessorItem( "parameter" );
        //     activeProcessorItem.SetAttribute( "name", "ActiveProcessors" );

        std::vector<std::string> activeProcs ;
        activeProcs.push_back("ActiveProcessors") ;

        std::vector<std::string> procConds ;
        procConds.push_back("ProcessorConditions") ;

        // variable used to check for duplicate processor entries in the execute section
        std::set<std::string> procList ;

        section = root->FirstChild("execute")  ;
        if( section != 0  ){

            // preprocess groups: replace with <processor/> tags
            replacegroups(  section )  ;

            // process processor conditions:
            processconditions( section , "" ) ;

            //       std::cout <<  " execute after group replacement : " << *section << std::

            TiXmlNode* proc = 0 ;
            while( ( proc = section->IterateChildren( "processor", proc ) )  != 0  ){

                std::string procName( getAttribute( proc, "name") );
                
                //std::cout << "looping over processor " + procName << std::endl;

                // exit if processor defined more than once in the execute section
                if( procList.find( procName ) != procList.end() ){
                    throw ParseException(std::string( "XMLParser::parse : "+ procName +" defined more than once in <execute> section ") );
                }
                procList.insert( procName ) ;

                activeProcs.push_back( procName ) ;

                std::string condition(  getAttribute( proc,"condition")  ) ;

                if( condition.size() == 0 ) 
                    condition += "true" ;

                procConds.push_back( condition  ) ;

            }

        } else {

            throw ParseException(std::string( "XMLParser::parse : no <execute/> section found in  ") 
                    + _fileName  ) ;
        }

        _current->add( activeProcs ) ;
        _current->add( procConds ) ;



        // preprocess groups: ---------------------------------------------------------------------------------
        // simply copy all group parameters to the processors
        // and then copy the processors to the root node <Marlin>
        while( (section = root->IterateChildren( "group", section ) )  != 0  ){

            std::vector<TiXmlNode*> groupParams ;

            TiXmlNode* param = 0 ;
            while( ( param = section->IterateChildren( "parameter" , param ) )  != 0  ){
                groupParams.push_back( param->Clone() ) ;
            }

            TiXmlNode* proc = 0 ;
            while( ( proc = section->IterateChildren( "processor" , proc ) )  != 0  ){

                for( std::vector<TiXmlNode*>::iterator it = groupParams.begin() ; it != groupParams.end() ; it++){
                    proc->InsertEndChild( **it ) ;
                }
                std::shared_ptr<TiXmlNode> clone( proc->Clone() ) ;
                root->InsertBeforeChild( section , *clone  ) ;   // FIXME: memory mngmt. ?
            }

            root->RemoveChild( section ) ;

            for( std::vector<TiXmlNode*>::iterator it = groupParams.begin() ; it != groupParams.end() ; it++){
                delete *it ;
            }
        }


        // process processor sections -------------------------------------------------------------------------
        std::vector<std::string> availableProcs ;
        availableProcs.push_back("AvailableProcessors") ;

        // count processors with collection type information in order to generate warning
        // about old files or missing type info
        std::pair<unsigned,unsigned> typeCount ;
        unsigned procCount(0) ; 
        unsigned typedProcCount(0) ;

        // use this variable to check for duplicate processor definitions
        procList.clear();

        while( (section = root->IterateChildren("processor",  section ) )  != 0  ){

            // std::cout << " processor found: " << section->Value() << std::endl ;

            std::string name( getAttribute( section, "name") ) ;
            _map[ name  ] = std::make_shared<StringParameters>();
            _current = _map[ name  ].get();

            // exit if processor defined more than once in the execute section
            if( procList.find( name ) != procList.end() ){
                throw ParseException(std::string( "XMLParser::parse : "+ name +" defined more than once in the steering file ") );
            }
            procList.insert( name ) ;


            // std::cout << " processor found: " << name << std::endl ;

            availableProcs.push_back( name ) ; 

            // add ProcessorType to processor parameters
            std::vector<std::string> procType(2)  ;
            procType[0] = "ProcessorType" ;
            procType[1] =   getAttribute( section, "type")  ;
            _current->add( procType ) ;



            std::pair<unsigned,unsigned> currentCount( typeCount ) ;

            parametersFromNode( section , &typeCount ) ;

            if( typeCount.first > currentCount.first || typeCount.second > currentCount.second ){
                ++typedProcCount ; // at least one type info attribute found in processor
            }
            //       else { 
            // 	std::cout << " -- processor w/o type info : " << name << std::endl ;
            //       }

            ++procCount ;
        }

        globalParameters->add( availableProcs )  ;

        // DEBUG:
        //_doc->SaveFile( "debug.xml" ) ;


        if( _forCCheck && typeCount.first==0 && typeCount.second ==0){
            std::cout  << "---------------------------------------------------------------------" << std::endl
                << "  WARNING XMLParser : none of the available processors have input or " << std::endl
                << "  or output collection information assigned. You won't be able to    " << std::endl 
                << "  check the steering file for consistency with 'Marlin -c  steer.xml'" << std::endl 
                << "  Please use Processor::registerInputCollection() and                " << std::endl 
                << "  Processor::registerOutputCollection()  in you Marlin processors    " << std::endl 
                << "  and create a new steering file with 'Marlin -x > newsteer.xml'     " << std::endl 
                << "  or add the appropriate information to your existing steering files " << std::endl 
                << "---------------------------------------------------------------------" << std::endl ;
        } 

        // fg --- this is not really a warning as there are a number of processors w/o any input or output collections
        //
        //     else if( procCount > typedProcCount ){
        //       std::cout  << "---------------------------------------------------------------------" << std::endl
        // 		 << "  WARNING XMLParser : some of the available processors don't have    " << std::endl
        // 		 << "  input or output collection information assigned. This is           " << std::endl 
        // 		 << "  needed to check the steering file for consistency with 'Marlin -c'." << std::endl 
        // 		 << "  Please use Processor::registerInputCollection() and                " << std::endl 
        // 		 << "  Processor::registerOutputCollection()  in you Marlin processors    " << std::endl 
        // 		 << "  and create a new steering file with 'Marlin -x > newsteer.xml'     " << std::endl 
        // 		 << "  or add the appropriate information to your existing steering files " << std::endl 
        // 		 << "---------------------------------------------------------------------" << std::endl ;
        //     }


	// ======    check if we have unused cmd line parameter overwrites
	if( _cmdlineparams.size() > 0 ) { 
	  
	  std::stringstream str ;
	  str  << "Unknwon command line parameter overwrite ( spelling !?) : \n " ;
	  
	  typedef CommandLineParametersMap::iterator IT ;
	  
	  for( IT itP=_cmdlineparams.begin() ; itP != _cmdlineparams.end() ; ++itP ){
	    
	    std::string index1 = itP->first ;
	    
	    typedef CommandLineParametersMap::mapped_type ValMap ;
	    
	    ValMap* clp_map = &itP->second ; 
	    
	    for( ValMap::iterator it = clp_map->begin() , end = clp_map->end() ;  it!=end ; ++it ) {  
	      str << "   " << index1 << "." << it->first << " : " << it->second << "\n"   ;
	    }
	  }
	  
	  str << " Note: only parameters that are present in the Marlin steering file can be overwritten !!! " << "\n"  ;
	  
	  throw ParseException( str.str() ) ;
	}
	//===================================================================
    }


    const char* XMLParser::getAttribute( TiXmlNode* node , const std::string& name ){

        TiXmlElement* el = node->ToElement() ;
        if( el == 0 ) 
            throw ParseException("XMLParser::getAttribute not an XMLElement " ) ;

        const char* at = el->Attribute( name.c_str() )  ;

        if( at == 0 ){

            std::stringstream str ;
            str  << "XMLParser::getAttribute missing attribute \"" << name 
                << "\" in element <" << el->Value() << "/> in file " << _fileName  ;
            throw ParseException( str.str() ) ;
        }

        return at ;

    }  


    void XMLParser::parametersFromNode(TiXmlNode* section, std::pair<unsigned,unsigned>*typeCount) { 

        TiXmlNode* par = 0 ;

        //_cmdlineparams["global"]["LCIOInputFiles"]="myfile.slcio" ;
        //_cmdlineparams["MyAIDAProcessor"]["FileName"]="myfile.root" ;

        std::string index1, index2, cmdlinevalues ;

	//  std::cout << " ******************************* "  <<std::endl ;

	index1 = section->Value() ;
	if( index1.compare( "processor" ) == 0 ){
	  index1 = getAttribute( section, "name") ;
	}
	
	// try and get a map of overwritten cmd line parameters for this processor
	typedef CommandLineParametersMap::mapped_type ValMap ;
	ValMap* clp_map = 0 ; 
	CommandLineParametersMap::iterator clp_it = _cmdlineparams.find( index1 ) ;
	if( clp_it != _cmdlineparams.end() ){  // found some command line parameters for this section
	  clp_map = &( clp_it->second ) ;
	}
	// CommandLineParametersMap::mapped_type clp_map = _cmdlineparams[ index1 ] ;


        while( ( par = section->IterateChildren( "parameter", par ) )  != 0  ){

            index2 = par->ToElement()->Attribute("name") ;

            // // case insensitive command line options
            // std::transform(index1.begin(), index1.end(), index1.begin(), ::toupper);
            // std::transform(index2.begin(), index2.end(), index2.begin(), ::toupper);
            
	    //std::cout << " ******** parameter found : " << par->ToElement()->Attribute("name") << std::endl ;
	    //std::cout << " ***** xml parameter: " << index1 << ": " << index2 << std::endl ;

            std::vector<std::string> tokens ;

            std::string name( getAttribute( par, "name" )  ) ;

            tokens.push_back(  name ) ; // first token is parameter name 

            LCTokenizer t( tokens ,' ') ;

            std::string inputLine("") ;


            try{  inputLine = getAttribute( par , "value" )  ; 
            }      
            catch( ParseException ) {

                if( par->FirstChild() )
                    inputLine =  par->FirstChild()->Value() ;
            }
            //       if( par->ToElement()->Attribute("value") != 0 ) {
            // 	inputLine = par->ToElement()->Attribute("value") ;
            //       }
            //       else if( par->FirstChild() ) {
            // 	inputLine =  par->FirstChild()->Value() ;
            //       }



	    // cmdlinevalues = _cmdlineparams[ index1 ][ index2 ] ;
            // if( cmdlinevalues.compare( "" ) != 0 ){
            //     inputLine = cmdlinevalues ; // overwrite steering file parameters with command line ones
            // }

	    // ---- check we have a cmd line param overwrite 	    
	    if( clp_map != 0 ) {   

	      ValMap::iterator vm_it = clp_map->find(  index2 ) ;

	      if( vm_it != clp_map->end() ) {
	      
		cmdlinevalues = vm_it->second ;

		if( cmdlinevalues.compare( "" ) != 0 ){

		  inputLine = cmdlinevalues ; // overwrite steering file parameters with command line ones

		  //	  std::cout << " ###############  will replace " << index1 << "." << index2 << " with : " << cmdlinevalues << std::endl ;

		  clp_map->erase( vm_it ) ;
		}
	      }
	    }

            // std::cout << " values : " << inputLine << std::endl ;

            std::for_each( inputLine.begin(),inputLine.end(), t ) ; 

            //        for( StringVec::iterator it = tokens.begin() ; it != tokens.end() ; it++ ){
            //  	std::cout << "  " << *it ;
            //        } 
            //        std::cout << std::endl ;

            _current->add( tokens ) ;



            //--------------- check for lcio input/output type attributes -----------

            std::vector<std::string> lcioInTypes ;
            std::vector<std::string> lcioOutTypes ;

            lcioInTypes.push_back( "_marlin.lcioInType" ) ;
            lcioOutTypes.push_back( "_marlin.lcioOutType" ) ;

            std::string colType("")  ;

            try{  

                colType = getAttribute( par , "lcioInType" )  ; 
                ++typeCount->first ; // count type description to identify old files w/o type description
                lcioInTypes.push_back( name ) ; 
                lcioInTypes.push_back( colType ) ; 

            }      
            catch( ParseException ) { }

            try{  

                colType = getAttribute( par , "lcioOutType" )  ; 
                ++typeCount->second ; // count type description to identify old files w/o type description
                lcioOutTypes.push_back( name ) ; 
                lcioOutTypes.push_back( colType ) ; 

            }      
            catch( ParseException ) { }



            _current->add( lcioInTypes  ) ; 
            _current->add( lcioOutTypes  ) ; 
        }


	// if we did have cmd line parameters and have used all of them, we delete the corresponding submap ... 
	if( clp_map != 0 && clp_map->size() == 0 ) { 
	  _cmdlineparams.erase( clp_it ) ;
	}

    }

    //   TiXmlElement* child2 = docHandle.FirstChild( "Document" ).FirstChild( "Element" ).Child( "Child", 1 ).Element();
    //   if ( child2 ){}

    std::shared_ptr<StringParameters> XMLParser::getParameters( const std::string& sectionName ) const {

        //     for( StringParametersMap::iterator iter = _map.begin() ; iter != _map.end() ; iter++){
        //       //     std::cout << " parameter section " << iter->first 
        //       // 	      << std::endl 
        //       // 	      << *iter->second 
        //       // 	      << std::endl ;
        //     }

        return _map[ sectionName ] ;
    }


    void XMLParser::processconditions( TiXmlNode* current , const std::string& aCondition ) {

        std::string condition ;
        // put parentheses around compound expressions 
        if( aCondition.find('&') != std::string::npos  ||  aCondition.find('|') != std::string::npos ) 
            condition = "(" + aCondition + ")" ;
        else
            condition = aCondition ;

        TiXmlNode* child = 0 ;
        while( ( child = current->IterateChildren( "if" , child )  )  != 0  ){

            processconditions( child , getAttribute( child, "condition") ) ;  
        }

        while( ( child = current->IterateChildren( "processor" , child )  )  != 0  ) {


            if(  child->ToElement()->Attribute("condition") == 0 ) {

                child->ToElement()->SetAttribute("condition" ,  condition ) ;

            } else {

                std::string cond( child->ToElement()->Attribute("condition") ) ; 

                if( cond.size() > 0 && condition.size() ) 
                    cond += " && " ;

                cond += condition ;

                child->ToElement()->SetAttribute("condition" ,  cond ) ;

            }


            if( std::string( current->Value() ) != "execute" ) {

                // unless we are already in the top node (<execute/>) we have to move all processors up

                TiXmlNode* parent = current->Parent() ;

                std::shared_ptr<TiXmlNode> clone( child->Clone() ) ;

                parent->InsertBeforeChild(  current , *clone ) ;  

            }

        }
        // remove the current <if/> node
        if( std::string( current->Value() ) != "execute" ) {
            TiXmlNode* parent = current->Parent() ;
            parent->RemoveChild( current ) ;
        }    

    }



    void XMLParser::replacegroups(TiXmlNode* section) {

        TiXmlElement* root = _doc->RootElement()  ;
        if( root==0 ){
            std::cout << "XMLParser::parse : no root tag <marlin>...</marlin> found ! " << std::endl ;
            return ;
        }

        TiXmlNode* child = 0 ;
        while( ( child = section->IterateChildren( child ) )  != 0  ){

            if( std::string( child->Value() )  == "group" ) {

                // find group definition in root node 
                TiXmlNode* group = findElement( root, "group", "name" , getAttribute( child, "name") ) ;

                if( group != 0 ) {

                    TiXmlNode* sub = 0 ;
                    while( ( sub = group->IterateChildren( "processor" , sub ) )  != 0  ){

                        // insert <processor/> tag
                        TiXmlElement item( "processor" );
                        item.SetAttribute( "name",  getAttribute( sub, "name") ) ;

                        section->InsertBeforeChild( child , item ) ;

                        // 	      std::cout << " inserting processor tag for group : " << item.Value() << ", " 
                        // 			<< item.Attribute("name") << std::endl ;
                    }

                    section->RemoveChild( child ) ; 

                } else

                    std::cout << " XMLParser::parse - group not found : " <<  child->ToElement()->Attribute("name") << std::endl ;

            } else  if( std::string( child->Value() )  == "if" )  {  // other element, e.g. <if></if>

                replacegroups( child ) ;

            }
        }
    }


    TiXmlNode* XMLParser::findElement( TiXmlNode* node , const std::string& type, 
            const std::string& attribute, const std::string& value ) {

        TiXmlNode* child = 0 ;
        bool elementFound  = false ;

        while( (child = node->IterateChildren( type , child ) )  != 0  ){

            if( std::string( *child->ToElement()->Attribute( attribute ) ) == value ) { 
                elementFound = true ;
                break ;
            }
        }
        if( ! elementFound ) 
            child = 0 ;

        return child ;
    }  

}  // namespace marlin



// -*- C++ -*-                                                                  
////////////////////////////////////////////////////////////////////////////////
///                                                                             
/// \file   src/public/bodyfunc.cc                                              
///                                                                             
/// \author Walter Dehnen                                                       
/// \date   2004-2006                                                           
///                                                                             
////////////////////////////////////////////////////////////////////////////////
//                                                                              
// Copyright (C) 2004-2006 Walter Dehnen                                        
//                                                                              
// This program is free software; you can redistribute it and/or modify         
// it under the terms of the GNU General Public License as published by         
// the Free Software Foundation; either version 2 of the License, or (at        
// your option) any later version.                                              
//                                                                              
// This program is distributed in the hope that it will be useful, but          
// WITHOUT ANY WARRANTY; without even the implied warranty of                   
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU            
// General Public License for more details.                                     
//                                                                              
// You should have received a copy of the GNU General Public License            
// along with this program; if not, write to the Free Software                  
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.                    
//                                                                              
////////////////////////////////////////////////////////////////////////////////
#ifdef falcON_NEMO
#include <public/bodyfunc.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>

extern "C" {
#include  <stdinc.h>                     // for nemo::string, needed in getparam
#include  <getparam.h>                   // getting name of main()              
#include  <loadobj.h>                    // loading shared object files         
#include  <filefn.h>                     // finding a function in a loaded file 
  typedef void        (*bf_pter)  (falcON::body const&,
				   double const&,
				   const falcON::real*);
  typedef void        (*Bf_pter)  (falcON::bodies const&,
				   double const&,
				   const falcON::real*);
  typedef void        (*Bm_pter)  (void*,
				   falcON::bodies const&,
				   double const&,
				   const falcON::real*);
}

using namespace falcON;
////////////////////////////////////////////////////////////////////////////////
namespace {

  using falcON::snprintf;

  const int debug_depth = 2;
  const size_t MAX_TYPE_LENGTH = 8;

  const char ParInd = '#';      ///< parameter indicator for bodyfunc expression

#if defined(DEBUG) || defined(EBUG)
  const char *OPTFLAGS = "-p -g";
#else
  const char *OPTFLAGS = "-O2";
#endif

  bool delete_f  = true;
  bool havesyms  = false;
  int  function  = 1; 
  int  testfunc  = 1; 

  typedef falcON::fieldset(*bd_pter)();
  typedef size_t          (*st_pter)();

  //////////////////////////////////////////////////////////////////////////////
  //                                                                          //
  // auxiliary classes & functions                                            //
  //                                                                          //
  //////////////////////////////////////////////////////////////////////////////
  using falcON::message;
  using falcON::exception;
  //----------------------------------------------------------------------------
  struct BfErr : public exception {
    BfErr(const char *m) : exception(m) {}
    BfErr(BfErr const&e) : exception(e) {}
  };
  //----------------------------------------------------------------------------
  struct ParseErr : public BfErr {
    ParseErr(const    char *m) : BfErr(m) {}
    ParseErr(ParseErr const&e) : BfErr(e) {}
  };
  //----------------------------------------------------------------------------
  struct DataBaseErr : public BfErr {
    DataBaseErr(const       char *m) : BfErr(m) {}
    DataBaseErr(DataBaseErr const&e) : BfErr(e) {}
  };
  //////////////////////////////////////////////////////////////////////////////
  void shrink(char*newexpr, size_t size, const char*expr) falcON_THROWING
  {
    // eats all white spaces from an expression                                 
    const char*e=expr;
    char      *n=newexpr;
    const char*u=newexpr+size;
    while(*e) {
      while(isspace(*e)) ++e;
      *(n++) = *(e++);
      if(n==u) falcON_THROW("shrinking expression exceeds size limit of %d\n",
			    size);
    }
    *n=0;
    DebugInfo(2,"shrink() expr = \"%s\"\n",newexpr);
  }
  //////////////////////////////////////////////////////////////////////////////
  inline void get_type(char*type, const char*name) throw(BfErr) {
    // finds function name and determines type from sizeof(name())              
    st_pter Size = (st_pter)findfn(const_cast<char*>(name));
    if(Size == 0) throw BfErr("cannot resolve type of expression");
    switch(Size()) {
    case 1:  SNprintf(type,MAX_TYPE_LENGTH,"bool"); break;
    case 2: 
    case 4:  SNprintf(type,MAX_TYPE_LENGTH,"int");  break;
    case 8:  SNprintf(type,MAX_TYPE_LENGTH,"real"); break;
    case 24: SNprintf(type,MAX_TYPE_LENGTH,"vect"); break;
    default: throw BfErr("cannot resolve type of expression");
    }
    DebugInfo(debug_depth,"get_type(): type=%s\n",type);
  }
  //----------------------------------------------------------------------------
  inline fieldset get_need(const char*name) throw(BfErr) {
    // finds function name and returns need                                     
    bd_pter Need = (bd_pter)findfn(const_cast<char*>(name));
    if(Need == 0) throw BfErr("cannot resolve fieldset need");
    fieldset need=Need();
    DebugInfo(debug_depth,"get_need(): need=%s\n",word(need));
    return need;
  }
  //----------------------------------------------------------------------------
  void compile(const char*flags, const char*fname) throw(BfErr) {
    // compiles a falcON C++ program in fname using compiler flags              
    const char* falcON_path = falcON::directory();
    if(falcON_path == 0) throw BfErr("cannot locate falcON directory");
    char cmmd[512];
    SNprintf(cmmd,512,"cd /tmp; %s %s.cc -o %s.so"
	     " %s -shared -fPIC -I%s/inc -I%s/inc/utils"
#ifdef  falcON_NEMO
	     " -I$NEMOINC -DfalcON_NEMO"
#endif
#ifdef  falcON_INDI
	     " -DfalcON_INDI"
#endif
#ifdef  falcON_SPH
	     " -DfalcON_SPH"
#endif
#if   defined(falcON_DOUBLE)
	     " -DfalcON_DOUBLE"
#elif defined(falcON_SINGLE)
	     " -DfalcON_SINGLE"
#endif
	     " >& %s.log",
	     getenv("CPATH")? "$CPATH/g++" : "g++",
	     fname,fname,(flags? flags : " "),falcON_path,falcON_path,fname);
    DebugInfo(2,"now compiling using the following command\n   %s\n",cmmd);
    if(system(cmmd)) {
      if(debug(debug_depth)) {
	std::cerr<<"could not compile temporary file /tmp/"<<fname<<".cc:\n";
	char show[512];
	SNprintf(show,512,"more /tmp/%s.cc > /dev/stderr",fname);
	system(show);
	std::cerr<<"\nwith the command\n\""<<cmmd<<"\".\n"
		 <<"Here is the output from the compiler:\n\n";
	SNprintf(show,512,"more /tmp/%s.log > /dev/stderr",fname);
	std::cerr<<'\n';
	system(show);
      }
      throw BfErr(message("could not compile expression; "
			  "perhaps it contains a syntax error"));
    }
  }
  //----------------------------------------------------------------------------
  inline void delete_files(const char*fname) {
    // delete files /tmp/fname.* UNLESS debug(debug_depth)
    if(delete_f && !debug(debug_depth) && fname && fname[0]) {
      char cmmd[512];
      SNprintf(cmmd,512,"rm -f /tmp/%s.* >& /dev/null",fname);
      DebugInfo(4,"executing \"%s\"\n",cmmd);
      system(cmmd);
    }
  }
  //----------------------------------------------------------------------------
  inline int digit(char const&c) {
    switch(c) {
    case '0': return 0;
    case '1': return 1;
    case '2': return 2;
    case '3': return 3;
    case '4': return 4;
    case '5': return 5;
    case '6': return 6;
    case '7': return 7;
    case '8': return 8;
    case '9': return 9;
    default : return -1;
    }
  }
  //----------------------------------------------------------------------------
  inline void simple_parse(const char*&in,         // I:     input              
			         char*&to,         // O:     output             
			   const char* toUP,       // I:     end for output     
			   int       & npar)       // I/O: # parameters         
    throw(ParseErr)
  {
    if(*in==ParInd) {                              // IF parameter indicator    
      ++in;                                        //   read indicator          
      int p = digit(*in++);                        //   read index              
      if(p < 0) throw ParseErr(message("'%c' not followed by digit",ParInd));
      if(p+1 > npar) npar = p+1;                   //   update max #parameters  
      if(to+6 >= toUP) throw ParseErr("expression too long");
      // Note. using sprintf here is safe, while SNprintf() causes a problem,
      // since we don't want a trailing 0 to be written.
      sprintf(to,"__P[%d]",p);                     //   add parameter to output 
      to += 6;                                     //   increment output        
    } else {                                       // ELSE                      
      *(to++) = *(in++);                           //     read expr into sexpr  
      if(to==toUP) throw ParseErr("expression too long");
    }                                              // ENDIF                     
  }
  //----------------------------------------------------------------------------
  inline void full_parse(const char*&in,           // I:     input              
			 char*      &to,           // O:     output             
			 const char* toUP,         // I:     end for output     
			 int        &npar)         // I/O: # parameters         
    throw(ParseErr)
  {
    try {
      while(*in)
	simple_parse(in,to,toUP,npar);
    } catch(ParseErr E) { throw E; }
  }
  //////////////////////////////////////////////////////////////////////////////
  //                                                                          //
  // BF_database                                                              //
  //                                                                          //
  //////////////////////////////////////////////////////////////////////////////
  class BF_database {
    static const size_t STR_LENGTH = 512;
    char          dir     [STR_LENGTH];
    char          fullfile[STR_LENGTH];
    mutable char  funcname[STR_LENGTH];
    bool          locked;
  public:
    //--------------------------------------------------------------------------
    // construction:                                                            
    // - set fullfile = $FALCONLIB/subdir/file                                  
    // - make sure that directory $FALCONLIB/subdir/ exists                     
    BF_database(const char*subdir,
		const char*file) throw(DataBaseErr) : locked(0)
    {
      // get falcON library directory
      const char*falcONlib = falcON::libdir();
      if(falcONlib==0) throw(DataBaseErr("falcON library path unknown"));
      SNprintf(dir,STR_LENGTH,"%s/%s",falcONlib,subdir);// falcONlib/subdir
      SNprintf(fullfile,STR_LENGTH,"%s/%s",dir,file);   // falcONlib/subdir/file
      // make sure falcONlib/subdir/ exists!
      char cmmd[STR_LENGTH];
      SNprintf(cmmd,STR_LENGTH,"cd %s >& /dev/null",falcONlib);
      DebugInfo(10,"executing \"%s\"\n",cmmd);
      if(system(cmmd)) throw(DataBaseErr(message("cannot %s",cmmd)));
      SNprintf(cmmd,STR_LENGTH,"cd %s/%s >& /dev/null",falcONlib,subdir);
      DebugInfo(10,"executing \"%s\"\n",cmmd);
      if(system(cmmd)) {
	DebugInfo(debug_depth,"BF_database: no directory %s/%s;"
		  " try to make it\n", falcONlib,subdir);
	SNprintf(cmmd,STR_LENGTH,"mkdir %s/%s >& /dev/null",falcONlib,subdir);
	DebugInfo(10,"executing \"%s\"\n",cmmd);
	if(system(cmmd)) throw(DataBaseErr(message("cannot %s",cmmd)));

	SNprintf(cmmd,STR_LENGTH,"chmod 777 %s/%s >& /dev/null",
		 falcONlib,subdir);
	DebugInfo(10,"executing \"%s\"\n",cmmd);
	if(system(cmmd)) throw(DataBaseErr(message("cannot %s",cmmd)));
      }
    }
    //--------------------------------------------------------------------------
    const char*directory() const {
      return dir;
    }
    //--------------------------------------------------------------------------
    bool const&is_locked() const {
      return locked;
    }
    //--------------------------------------------------------------------------
    // print info about bodyfuncs in base to output
    bool printinfo(std::ostream&out) const {
      std::ifstream file(fullfile);
      if(file.is_open()) {
	const size_t FNAME_SIZE = 256;
	char     fexp[FNAME_SIZE], fold[FNAME_SIZE], type;
	int      npar,num=1;
	fieldset need;
	out << 
	  "# contents of bodyfunc database:\n"
	  "#----+----------------------------------+------+----------+------+\n"
	  "# No | expression (compact form)        | type | need     | npar |\n"
	  "#----+----------------------------------+------+----------+------+\n";
	while(!file.eof()) {
	  file >> fexp >> type >> npar >> need >> funcname;
	  if(num==1 || strcmp(fexp,fold)) {
	    strncpy(fold,fexp,FNAME_SIZE);
	    out  << '#'
		 << std::setw(3)  << num++ << " | "
		 << std::setw(32) << fexp  << " | "
		 << ( type == 'b'? "bool | " :
		      type == 'i'? "int  | " :
		      type == 'r'? "real | " : "vect | " )
		 << std::setw(8) << word(need) << " | "
		 << std::setw(4) << npar << " |\n";
	  }
	}
	out << 
	  "#----+----------------------------------+------+----------+------+"
	    << std::endl;
	return true;
      }
      return false;
    }
    //--------------------------------------------------------------------------
    // findfunc():   try to find a bodyfunc from expr                           
    const char*findfunc(                           // R: function name          
			const char*expr,           // I: expression to search   
			char      &type,           // O: function type          
			int       &npar,           // O: number of parameters   
			fieldset  &need) const {   // O: function need          
      // try to open full file for reading
      std::ifstream file(fullfile);
      if(file.is_open()) {
	// read full file and search for matching expression
	char fexp[512];
	while(file) {
	  file >> fexp >> type >> npar >> need >> funcname;
	  if(0 == strcmp(expr,fexp)) return funcname;
	}
      }
      return 0;
    }
    //--------------------------------------------------------------------------
    // counter():                                                               
    // returns a valid function counter OR throws an exception                  
    // - if database is locked,     - throw DataBaseErr                         
    // - if database doesn't exist, - create backup file and lock it            
    //                              - return 1                                  
    // - otherwise:                 - copy to backup file and lock it           
    //                              - loop database, count functions            
    //                              - return count                              
    int counter() throw(DataBaseErr) {             // R: valid function counter 
      char cmmd[STR_LENGTH];
      // check for existence of backup-file
      SNprintf(cmmd,STR_LENGTH,"ls %s.bak >& /dev/null",fullfile);
      DebugInfo(10,"executing \"%s\"\n",cmmd);
      if(!system(cmmd)) throw DataBaseErr(message("file %s/%s.bak exists"));
      char fbak[STR_LENGTH];
      SNprintf(fbak,STR_LENGTH,"%s.bak",fullfile);
      // try to open fullfile for reading
      std::ifstream file(fullfile);
      if(!file.is_open()) {
	// if fullfile is unreadable,
	//   touch fullfile.bak and lock that; return counter = 1
	SNprintf(cmmd,STR_LENGTH,"touch %s; chmod 000 %s",fbak,fbak);
	DebugInfo(10,"executing \"%s\"\n",cmmd);
	if(system(cmmd)) throw(DataBaseErr(message("cannot %s",cmmd)));
	locked = 1;
	return 1;
      } else {
	// if fullfile is readable,
	//   copy to fullfile.bak and lock that
	//   read fullfile to find counter
	SNprintf(cmmd,STR_LENGTH,"cp %s %s; chmod 000 %s", fullfile,fbak,fbak);
	DebugInfo(10,"executing \"%s\"\n",cmmd);
	if(system(cmmd)) throw(DataBaseErr(message("cannot %s",cmmd)));
	locked = 1;
	char c;
	if(file.eof()) return 1;
	int  n = 0;
	while(file.good()) {
	  file.get(c);
	  if(c == '\n') n++;
	}
	return n;
      }
    }
    //--------------------------------------------------------------------------
    void unlock() {
      if(locked) {
	char cmmd[STR_LENGTH];
	SNprintf(cmmd,STR_LENGTH,
		 "mv %s.bak %s >& /dev/null; chmod 666 %s >& /dev/null",
		 fullfile,fullfile,fullfile);
	DebugInfo(10,"executing \"%s\"\n",cmmd);
	if(system(cmmd)) falcON_Warning("problems unlocking database\n");
	locked = 0;
      }
    }
    //--------------------------------------------------------------------------
    // put():                                                                   
    // If database is locked, append new entry to fullfile.bak                  
    void put(                                      // R: success?               
	     const    char *fname,                 // I: file name              
	     const    char *func,                  // I: function name          
	     const    char *expr,                  // I: expression encoded     
	     char     const&type,                  // I: function type          
	     int      const&npar,                  // I: number of parameters   
	     fieldset const&need)                  // I: function need          
      throw(DataBaseErr)
    {
      if(!locked) throw DataBaseErr("not locked, cannot put()");
      char cmmd[STR_LENGTH];
      SNprintf(cmmd,STR_LENGTH,"cp /tmp/%s.so %s/%s.so >& /dev/null; "
	       "chmod 444 %s/%s.so >& /dev/null",
	       fname,dir,func,dir,func);
      DebugInfo(10,"executing \"%s\"\n",cmmd);
      if(system(cmmd))
	throw DataBaseErr(message("cannot copy file /tmp/%s.so into base",
				  fname));
      char fbak[STR_LENGTH];
      SNprintf(fbak,STR_LENGTH,"%s.bak",fullfile);
      SNprintf(cmmd,STR_LENGTH,"chmod 600 %s >& /dev/null",fbak);
      DebugInfo(10,"executing \"%s\"\n",cmmd);
      if(system(cmmd)) throw DataBaseErr(message("cannot %s",cmmd));
      std::ofstream file;
      if(!open_to_append(file,fbak) )
	throw DataBaseErr(message("cannot open file %s",fbak));
      file  <<expr<<' '<<type<<' '<<npar<<' '<<need<<' '<<func<<std::endl;
      SNprintf(cmmd,STR_LENGTH,"chmod 000 %s >& /dev/null",fbak);
      DebugInfo(10,"executing \"%s\"\n",cmmd);
      if(system(cmmd)) throw DataBaseErr(message("cannot %s",cmmd));
    }
    //--------------------------------------------------------------------------
    // destructor: unlock                                                       
    ~BF_database() {
      unlock();
    }
  };
  //////////////////////////////////////////////////////////////////////////////
  //                                                                          //
  // get_type()                                                               //
  //                                                                          //
  // determines the return type and the required body data of a given         //
  // bodyfunc expression. To this end, we employ the compiler to compile      //
  // a simple file and then load it and call the functions to retrieve the    //
  // desired information.                                                     //
  //                                                                          //
  //////////////////////////////////////////////////////////////////////////////
  void get_type(char      *type,            // O: return type                   
		fieldset  &need,            // O: body data required            
		const char*expr)            // I: C-expression encoded in func  
    throw(BfErr)
  {
    // P   preparations
    if (!havesyms) {
      mysymbols(getargv0());
      havesyms = true;
    }
    // 1 create C++ file implementing functions fneed and fsize
    const size_t FNAME_SIZE=128;
    char fname[FNAME_SIZE], fneed[FNAME_SIZE], fsize[FNAME_SIZE],
         ffile[FNAME_SIZE];
    SNprintf(fname,FNAME_SIZE,"bf_t_%s_%d",RunInfo::pid(),testfunc);
    SNprintf(ffile,FNAME_SIZE,"/tmp/%s.cc",fname);
    SNprintf(fneed,FNAME_SIZE,"bf_need_%d",testfunc);
    SNprintf(fsize,FNAME_SIZE,"bf_size_%d",testfunc++);
    std::ofstream file(ffile);
    if(!file) 
      throw BfErr(message("cannot create temporary file \"%s\"",ffile));
    file << 
      "//\n"
      "// file "<<ffile<<" generated by get_type()\n//\n"
      "#include <cmath>\n"
      "#include <body.h>\n\n"
      "using namespace falcON;\n\n"
      "#define BD_TEST\n"
      "#define body_func\n"
      "#include <public/bodyfuncdefs.h>\n\n"
      "real   __P[10]={RNG()};\n\n"
      "extern \"C\" {\n"
      "  fieldset "<<fneed<<"()\n"
      "  {\n"
      "    double t=0.;\n"
      "    double __X = abs("<<expr<<");\n"
      "    return need;\n"
      "  }\n\n"
      "  size_t "<<fsize<<"()\n"
      "  {\n"
      "    double t=0.;\n"
      "    return sizeof("<<expr<<");\n"
      "  }\n"
      "}\n";
    file.close();
    try {
      // 2 compile the C++ file and create a shared object file
      compile(0,fname);
      // 3 load the .so file and find out about need and type
      SNprintf(ffile,FNAME_SIZE,"/tmp/%s.so",fname);
      loadobj(ffile);
      get_type(type,fsize);
      need = get_need(fneed);
    } catch(BfErr E) {
      delete_files(fname);
      throw E;
    }
    delete_files(fname);
  } // get_type()
  //////////////////////////////////////////////////////////////////////////////
  //                                                                          //
  // make_func                                                                //
  //                                                                          //
  // make a bodyfunc function of given type and name.                         //
  // On successful return, the function                                       //
  //   type name(b,t);                                                        //
  // will be in file "/tmp/name.so"                                           //
  // The files "/tmp/name.cc" and "/tmp/name.log" will NOT be deleted         //
  //                                                                          //
  //////////////////////////////////////////////////////////////////////////////
  bf_pter make_func(                        // R: bodyfunc                      
		    const char*expr,        // I: C-expression to be encoded    
		    const char*ftype,       // I: function return type          
		    const char*fname,       // I: file base name                
		    const char*funcn)       //[I: function name]                
    throw(BfErr)
  {
    // P   preparations
    if (!havesyms) {
      mysymbols(getargv0());
      havesyms = true;
    }
    // 1 create C++ file implementing the function
    const size_t FNAME_SIZE=256;
    char ffile[FNAME_SIZE], _func[FNAME_SIZE];
    const char*ffunc;
    if(funcn && funcn[0])
      ffunc = funcn;
    else {
      SNprintf(_func,FNAME_SIZE,"%s%d",fname,function++);
      ffunc = _func;
    }
    SNprintf(ffile,FNAME_SIZE,"/tmp/%s.cc",fname);
    std::ofstream file(ffile);
    if(!file) 
      throw BfErr(message("cannot create temporary file \"%s\"",ffile));
    file << 
      "//\n//\n"
      "// file "<<ffile<<" generated by make_func\n//\n"
      "#include <cmath>\n"
      "#include <body.h>\n\n"
      "using namespace falcON;\n\n"
      "#undef BD_TEST\n"
      "#define body_func\n"
      "#include <public/bodyfuncdefs.h>\n\n"
      "extern \"C\" {\n"
      "  "<<ftype<<
      "  "<<ffunc<<
      "(falcON::body const&b, double const&t, const real*__P)\n"
      "  {\n"
      "    return ("<<expr<<");\n"
      "  }\n"
      "}\n";
    file.close();
    // 2 compile the C++ file and create a shared object file
    compile(OPTFLAGS,fname);
    // 3 load the .so file and find our function are return it
    SNprintf(ffile,FNAME_SIZE,"/tmp/%s.so",fname);
    loadobj(ffile);
    bf_pter func = (bf_pter)findfn(const_cast<char*>(ffunc));
    if(func == 0)
      throw BfErr(message("couldn't find function \"%s\"\n",ffunc));
    return func;
  } // make_func()
  //////////////////////////////////////////////////////////////////////////////
  //                                                                          //
  // make_method                                                              //
  //                                                                          //
  // make a bodiesmethod function of given type and name.                     //
  // On successful return, the function                                       //
  //   type name(b,t);                                                        //
  // will be in file "/tmp/name.so"                                           //
  // The files "/tmp/name.cc" and "/tmp/name.log" will NOT be deleted         //
  //                                                                          //
  //////////////////////////////////////////////////////////////////////////////
  Bm_pter make_method(                      // R: bodiesmethod                  
		      const char*expr,      // I: C-expression to be encoded    
		      const char*ftype,     // I: function return type          
		      const char*fname,     // I: file base name                
		      const char*funcn)     //[I: function name]                
    throw(BfErr)
  {
    // P   preparations
    if (!havesyms) {
      mysymbols(getargv0());
      havesyms = true;
    }
    // 1 create C++ file implementing the method
    const size_t FNAME_SIZE=256;
    char ffile[FNAME_SIZE], _func[FNAME_SIZE];
    const char*ffunc;
    if(funcn && funcn[0])
      ffunc = funcn;
    else {
      SNprintf(_func,FNAME_SIZE,"%s%d",fname,function++);
      ffunc = _func;
    }
    SNprintf(ffile,FNAME_SIZE,"/tmp/%s.cc",fname);
    std::ofstream file(ffile);
    if(!file) 
      throw BfErr(message("cannot create temporary file \"%s\"",ffile));
    file << 
      "//\n//\n"
      "// file "<<ffile<<" generated by make_method\n//\n"
      "#include <cmath>\n"
      "#include <body.h>\n\n"
      "using namespace falcON;\n\n"
      "#undef BD_TEST\n"
      "#define body_func\n"
      "#include <public/bodyfuncdefs.h>\n\n"
      "extern \"C\" {\n"
      "  void "<<ffunc<<
      "(void*__X, falcON::bodies const&B, double const&t, const real*__P)\n"
      "  {\n"
      "    LoopAllBodies(&B,b)\n"
      "      static_cast<"<<ftype<<"*>(__X)[i] = ("<<expr<<");\n"
      "  }\n"
      "}\n";
    file.close();
    // 2 compile the C++ file and create a shared object file
    compile(OPTFLAGS,fname);
    // 3 load the .so file and find our function are return it
    SNprintf(ffile,FNAME_SIZE,"/tmp/%s.so",fname);
    loadobj(ffile);
    Bm_pter func = (Bm_pter)findfn(const_cast<char*>(ffunc));
    if(func == 0)
      throw BfErr(message("couldn't find function \"%s\"\n",ffunc));
    return func;
  } // make_method()
  //////////////////////////////////////////////////////////////////////////////
  //                                                                          //
  // auxiliary data used for get_bodiesfunc()                                 //
  //                                                                          //
  //////////////////////////////////////////////////////////////////////////////


  const char sep = '@';        // seperator between subcondition & subexpression

  const size_t MAX_OPNAME_LENGTH  = 6;
  const size_t MAX_NUMBER_SUBEXPR = 17;
  const size_t MAX_LENGTH_SUBEXPR = 128;
  const size_t MAX_LENGTH_EXPR    = 1024;
  typedef char *pchar;

  char  nexpr  [MAX_LENGTH_EXPR];
  char  subtype[MAX_NUMBER_SUBEXPR][MAX_TYPE_LENGTH];
  char  subname[MAX_NUMBER_SUBEXPR][MAX_TYPE_LENGTH];
  char  subexpr[MAX_NUMBER_SUBEXPR][MAX_LENGTH_SUBEXPR];
  pchar sexpr  [MAX_NUMBER_SUBEXPR] = {0};
  pchar scond  [MAX_NUMBER_SUBEXPR] = {0};
  pchar stype  [MAX_NUMBER_SUBEXPR] = {0};
  pchar sname  [MAX_NUMBER_SUBEXPR] = {0};
  int   soper  [MAX_NUMBER_SUBEXPR];
  int   sub, par;

  const char* const OpName[9] = {
    "Mean", "Mmean", "Sum", "Max", "Min", "And", "Or", "Num", 0 };
  const size_t  OpNameSize[9] = { 4,5,3,3,3,3,2,3,0 };

  //////////////////////////////////////////////////////////////////////////////
  //                                                                          //
  // parse_expr()                                                             //
  //                                                                          //
  // parses a bodiesfunc expression:                                          //
  // a bodiesfunc expression may contain operations of the form               //
  //                                                                          //
  //    Operator{ bodyfunc subexpression }                                    //
  //                                                                          //
  // Here, we find those and list the subexpressions in sexpr[].              //
  // The operators are stored in soper[] and in sname[] the names of funcs.   //
  //                                                                          //
  //////////////////////////////////////////////////////////////////////////////
  bool inline is_oper(const char* &expr, int& Op) {
    if(!isupper(*expr)) return 0;
    for(int op=0; OpName[op]; ++op)
      if(0==strncmp(expr,OpName[op],OpNameSize[op])) {
	Op   = op;
	expr+= OpNameSize[op];
	return true;
      }
    return false;
  }
  //----------------------------------------------------------------------------
  bool ParseExpr(                                  // R: finished with '}' ?    
		 const char*&expr)                 // bodiesfunc expression     
    throw(ParseErr)
  {
    int we    = sub++;                             // remember sexpr counter    
    scond[we] = 0;                                 // reset subconditional      
    sexpr[we] = subexpr[we];                       // set new subexpr           
    sname[we] = subname[we];                       // set new subname           
    SNprintf(sname[we],MAX_TYPE_LENGTH,"__S%02d",we); // write subname          
    char* sex = sexpr[we];                         // pter in subexpr           
    char* const sexUP = sex + MAX_LENGTH_SUBEXPR;  // ceiling of subexpr        
    while(*expr) {                                 // LOOP through expression   
      while(isspace(*expr)) ++expr;                //   eat white space         
      if       (is_oper(expr,soper[sub])) {        //   IF matches Operator     
	if(sex+5 >= sexUP)
	  throw ParseErr("expression too long");
	// Note. using sprintf() is safe here. SNprintf() wouldn't, though.
	sprintf(sex,"__S%02d",sub);                //     add func call to sexpr
	sex += 5;                                  //     increment sex         
       	if(*expr!='{')
	  throw ParseErr(message("'%s' must be followed by '{'",
				 OpName[soper[sub]]));
	++expr;                                    //     read '{'              
	if(! ParseExpr(expr) )                     //     IF end of input       
	  throw ParseErr("unexpected end of expression");
      } else if(*expr=='{') {                      //   ELIF second '{'         
	++expr;                                    //     read '{'              
	if(we)
	  throw ParseErr(message("too many '{' after operator '%s'",
				 OpName[soper[we]]));
	else
	  throw ParseErr("'{' outside of operator");
      } else if(*expr=='}') {                      //   ELIF closing '}'        
	++expr;                                    //     read '}'              
	*sex = 0;                                  //     close subexpression   
	return true;                               //     done parsing sexpr    
      } else if(*expr==sep) {                      //   ELIF seperator found    
	++expr;                                    //     read seperator        
	if(we==0)
	  throw ParseErr(message("'%c' outside of operator",sep));
	else if (scond[we])
	  throw ParseErr(message("too many '%c' in operator '%s'",sep,
				 OpName[soper[we]]));
	*(sex++)  = 0;                             //     close subexpression   
	if(sex==sexUP)
	  throw ParseErr("expression too long");
	scond[we] = sex;                           //     open subcondition     
// 	scond[we] = sexpr[we];                     //     get subcondition      
// 	*(sex++)  = 0;                             //     close subconditions   
// 	if(sex==sexUP)
// 	  throw ParseErr("expression too long");
// 	sexpr[we] = sex;                           //     reset subexpr         
      } else                                       //   ELSE                    
	simple_parse(expr,sex,sexUP,par);          //     parse like bodyfunc   
    }                                              // END LOOP                  
    return false;                                  // end of expr               
  } // ParseExpr
  //----------------------------------------------------------------------------
  inline void parse_expr(const char*expr) throw(ParseErr)
  {
    sub = 0;
    par = 0;
    soper[0] = 8;
    if(ParseExpr(expr)) throw ParseErr("too many '}'");
    if(scond[0])        throw ParseErr(message("'%c' outside operator",sep));
    for(int s=0; s!=sub; ++s)
      if(soper[s] == 7) {
	if(scond[s])
	  throw ParseErr(message("'%c' inside operator 'Num'",sep));
	scond[s] = sexpr[s];
	sexpr[s] = 0;
      } else if(sexpr[s][0]==0) {
	if(s)
	  throw ParseErr(message("empty subexpression in operator '%s'",
				 OpName[soper[s]]));
	else
	  throw ParseErr("empty expression");
      } else if(scond[s] && !scond[s][0]) {
	throw ParseErr(message("empty subcondition in operator '%s'",
			       OpName[soper[s]]));
      }
  } // parse_expr
  //////////////////////////////////////////////////////////////////////////////
  //                                                                          //
  // get_types()                                                              //
  //                                                                          //
  // uses the sexpr, soper, sname output from parse_expr to find out about    //
  // the types of the subexpressions, the total expression, and the fieldset  //
  // needs.                                                                   //
  //                                                                          //
  //////////////////////////////////////////////////////////////////////////////
  void get_types(fieldset&need) throw(BfErr) {
    // P   preparations
    if (!havesyms) {
      mysymbols(getargv0());
      havesyms = true;
    }
    // 1 create C++ file implementing functions fneed() and fsize()
    const size_t FNAME_SIZE=256;
    char fname[FNAME_SIZE], fneed[FNAME_SIZE], fsize[FNAME_SIZE],
         ffile[FNAME_SIZE];
    SNprintf(fname,FNAME_SIZE,"Bf_t_%s_%d",RunInfo::pid(),testfunc);
    SNprintf(ffile,FNAME_SIZE,"/tmp/%s.cc",fname);
    SNprintf(fneed,FNAME_SIZE,"Bf_need_%d",testfunc);
    SNprintf(fsize,FNAME_SIZE,"Bf_size_%d",testfunc++);
    std::ofstream file(ffile);
    if(!file)
      throw BfErr(message("cannot create temporary file \"%s\"",ffile));
    file    <<"//\n"
	    <<"// file "<<ffile<<" generated by get_types()\n"
	    <<"//\n"
	    <<"#include <cmath>\n"
	    <<"#include <body.h>\n"
	    <<"\n"
	    <<"using namespace falcON;\n"
	    <<"\n"
	    <<"#define BD_TEST\n"
	    <<"#define bodies_func\n"
	    <<"#include <public/bodyfuncdefs.h>\n"
	    <<"\n"
	    <<"namespace {\n"
	    <<"  double t=0.;\n"
	    <<"  real __P[10]={RNG()};\n"
	    <<"}\n"
	    <<"\n"
	    <<"extern \"C\" {\n";
    for(int s=sub-1; s >= 0; --s)
      if(soper[s]==7)
	file<<"\n"
	    <<"# define "<<sname[s]<<" 1\n";
      else
	file<<"\n"
	    <<"# define "<<sname[s]<<" ("<<sexpr[s]<<")\n"
	    <<"  size_t "<<fsize<<'_'<<s<<"() {\n"
	    <<"    return sizeof("<<sexpr[s]<<"); }\n";
    file    <<"\n"
	    <<"  fieldset "<<fneed<<"() {\n";
    for(int s=0; s != sub; ++s) {
      if(scond[s])
	file<<"    bool   __B"<<s<<" = cond("<<scond[s]<<");\n";
      if(soper[s] != 7)
	file<<"    double __X"<<s<<" = abs ("<<sexpr[s]<<");\n";
    }
    file    <<"    return need;\n"
	    <<"  }\n"
	    <<"} // extern \"C\"\n";
    file.close();
    try {
      // 2 compile the C++ file and create a shared object file
      compile(0,fname);
      // 3 load the .so file and find out about need and types
      SNprintf(ffile,FNAME_SIZE,"/tmp/%s.so",fname);
      loadobj(ffile);
      need = get_need(fneed);
      for(int s=0; s!=sub; ++s) {
	if(soper[s] == 1) need |= fieldset::m;
	stype[s] = subtype[s];
	if(soper[s] == 7)
	  SNprintf(stype[s],MAX_TYPE_LENGTH,"int");
	else {
	  char fsizesub[FNAME_SIZE];
	  SNprintf(fsizesub,FNAME_SIZE,"%s_%d",fsize,s);
	  get_type(stype[s],fsizesub);
	}
      }
    } catch (BfErr E) {
      delete_files(fname);
      throw E;
    }
    delete_files(fname);
  }
  //////////////////////////////////////////////////////////////////////////////
  //                                                                          //
  // make_func                                                                //
  //                                                                          //
  // uses the sexpr, sname, soper, stype to generate a bodies_func            //
  // On successful return, the function                                       //
  //   type name(B,t);                                                        //
  // will be in file "/tmp/name.so"                                           //
  // The files "/tmp/name.cc" and "/tmp/name.log" will NOT be deleted         //
  //                                                                          //
  //////////////////////////////////////////////////////////////////////////////
  inline void make_mean(std::ostream&file, int s) throw(ParseErr) {
    if(stype[s][0] == 'b')
      throw ParseErr("operator 'Mean' must have non-boolean expression");
    const char *space = scond[s]? "      " : "    ";
    file  <<"    // encoding \"Mean{"<<sexpr[s];
    if(scond[s])
      file<<sep<<scond[s];
    file  <<"}\"\n"
	  <<"    register "<<stype[s]<<" __X("
	  << (stype[s][0]=='i'? "0":"zero)") << ";\n"
	  <<"    register int __N = 0;\n"
	  <<"    LoopAllBodies(&B, b)";
    if(scond[s])
      file<<"\n      if(cond("<<scond[s]<<"))";
    file  <<"{\n"
	  <<space<<"  __X += "<<sexpr[s]<<";\n"
	  <<space<<"  __N ++;\n"
	  <<space<<"}\n"
	  <<"    return __X / "<<(stype[s][0]=='i'? "__N":"real(__N)")<<";\n";
  }
  //----------------------------------------------------------------------------
  inline void make_mmean(std::ostream&file, int s) throw(ParseErr) {
    if(stype[s][0] == 'b')
      throw ParseErr("operator 'Mmean' must have non-boolean expression");
    const char *space = scond[s]? "      " : "    ";
    file  <<"    // encoding \"Mmean{"<<sexpr[s];
    if(scond[s])
      file<<sep<<scond[s];
    file  <<"}\"\n"
	  <<"    register "<<stype[s]<<" __X("
	  << (stype[s][0]=='i'? "0":"zero)") << ";\n"
	  <<"    register real __M(zero);\n"
	  <<"    LoopAllBodies(&B, b)";
    if(scond[s])
      file<<"\n      if(cond("<<scond[s]<<"))";
    file  <<"{\n"
	  <<space<<"  __X += m*("<<sexpr[s]<<");\n"
	  <<space<<"  __M += m;\n"
	  <<space<<"}\n"
	  <<"    return __X / __M;\n";
  }
  //----------------------------------------------------------------------------
  inline void make_sum(std::ostream&file, int s) throw(ParseErr) {
    if(stype[s][0] == 'b')
      throw ParseErr("operator 'Sum' must have non-boolean expression");
    file  <<"    // encoding \"Sum{"<<sexpr[s];
    if(scond[s])
      file<<sep<<scond[s];
    file  <<"}\"\n"
	  <<"    register "<<stype[s]<<" __X("
	  << (stype[s][0]=='i'? "0":"zero)") << ";\n"
	  <<"    LoopAllBodies(&B, b)\n";
    if(scond[s])
      file<<"      if(cond("<<scond[s]<<"))\n  ";
    file  <<"      __X += "<<sexpr[s]<<";\n"
	  <<"    return __X;\n";
  }
  //----------------------------------------------------------------------------
  inline void make_max(std::ostream&file, int s) throw(ParseErr) {
    if(stype[s][0] == 'b')
      throw ParseErr("operator 'Max' must have non-boolean expression");
    const char *space = scond[s]? "      " : "    ";
    file  <<"    // encoding \"Max{"<<sexpr[s];
    if(scond[s])
      file<<sep<<scond[s];
    file  <<"}\"\n"
	  <<"    body b=B.begin_all_bodies();\n";
    if(scond[s])
      file<<"    while(! cond("<<scond[s]
	  <<") && b != B.end_all_bodies()) ++b;\n";
    file  <<"    if(b == B.end_all_bodies()) {\n"
	  <<"      falcON_Warning(\"Max{";
    if(scond[s])
      file<<scond[s]<<' '<<sep<<' ';
    file  <<sexpr[s]<<"}: nobody "
	  <<(scond[s]?"satisfies condition":"present")<<"\");\n"
	  <<"      return "<<(stype[s][0]=='i'? "0":
			      stype[s][0]=='v'? "vect(zero)":"zero") << ";\n"
	  <<"    }\n"
	  <<"    register "<<stype[s]<<" __X = "<<sexpr[s]<<";\n"
	  <<"    for(++b; b!=B.end_all_bodies(); ++b)\n";
    if(scond[s])
      file<<"      if(cond("<<scond[s]<<"))\n";
    file  <<space<<"  update_max(__X,"<<sexpr[s]<<");\n"
	  <<"    return __X;\n";
  }
  //----------------------------------------------------------------------------
  inline void make_min(std::ostream&file, int s) throw(ParseErr) {
    if(stype[s][0] == 'b')
      throw ParseErr("operator 'Min' must have non-boolean expression");
    const char *space = scond[s]? "      " : "    ";
    file  <<"    // encoding \"Min{"<<sexpr[s];
    if(scond[s])
      file<<sep<<scond[s];
    file  <<"}\"\n"
	  <<"    body b=B.begin_all_bodies();\n";
    if(scond[s])
      file<<"    while(! cond("<<scond[s]
	  <<") && b != B.end_all_bodies()) ++b;\n";
    file  <<"    if(b == B.end_all_bodies()) {\n"
	  <<"      falcON_Warning(\"Min{";
    if(scond[s])
      file<<scond[s]<<' '<<sep<<' ';
    file  <<sexpr[s]<<"}: nobody "
	  <<(scond[s]?"satisfies condition":"present")<<"\");\n"
	  <<"      return "<<(stype[s][0]=='i'? "0":
			      stype[s][0]=='v'? "vect(zero)":"zero") << ";\n"
	  <<"    }\n"
	  <<"    register "<<stype[s]<<" __X = "<<sexpr[s]<<";\n"
	  <<"    for(++b; b!=B.end_all_bodies(); ++b)\n";
    if(scond[s])
      file<<"      if(cond("<<scond[s]<<"))\n";
    file  <<space<<"  update_min(__X,"<<sexpr[s]<<");\n"
	  <<"    return __X;\n";
  }
  //----------------------------------------------------------------------------
  inline void make_and(std::ostream&file, int s) {
    file  <<"    // encoding \"And{"<<sexpr[s];
    if(scond[s])
      file<<sep<<scond[s];
    file  <<"}\"\n"
	  <<"    LoopAllBodies(&B, b)\n"
	  <<"      if(";
    if(scond[s])
      file<<"cond("<<scond[s]<<") && ";
    file  <<"! ("<<sexpr[s]<<") ) return false;\n"
	  <<"    return true;\n";
  }
  //----------------------------------------------------------------------------
  inline void make_or(std::ostream&file, int s) {
    file  <<"    // encoding \"Or{"<<sexpr[s];
    if(scond[s])
      file<<sep<<scond[s];
    file  <<"}\"\n"
	  <<"    LoopAllBodies(&B, b)\n"
	  <<"      if(";
    if(scond[s])
      file<<"cond("<<scond[s]<<") && ";
    file  <<" ("<<sexpr[s]<<") ) return true;\n"
	  <<"    return false;\n";
  }
  //----------------------------------------------------------------------------
  inline void make_num(std::ostream&file, int s) throw(ParseErr) {
    if(scond[s]==0 || scond[s][0]==0)
      throw ParseErr("empty condition for operator 'Num'");
    file  <<"    // encoding \"Num{"<<scond[s]<<"}\"\n"
	  <<"    register int __N = 0;\n"
	  <<"    LoopAllBodies(&B, b)\n"
	  <<"      if(cond("<<scond[s]<<")) ++ __N;\n"
	  <<"    return __N;\n";
  }
  //----------------------------------------------------------------------------
  void make_sub(std::ostream&file, int s) throw(ParseErr) {
    file<<"\n  inline "<<stype[s]<<' '<<sname[s]<<'F'
	<<"(bodies const&B, double const&t, const real*__P) {\n";
    switch(soper[s]) {
    case 0: make_mean (file,s); break;
    case 1: make_mmean(file,s); break;
    case 2: make_sum  (file,s); break;
    case 3: make_max  (file,s); break;
    case 4: make_min  (file,s); break;
    case 5: make_and  (file,s); break;
    case 6: make_or   (file,s); break;
    case 7: make_num  (file,s); break;
    default: throw ParseErr("unknown operator");
    }
    file<<"  }\n"
	<<"  "<<stype[s]<<' '<<sname[s]<<";\n";
  }
  //----------------------------------------------------------------------------
  Bf_pter make_func(                        // R: bodiesfunc                    
		    const char*fname,       // I: file base name                
		    const char*funcn)       //[I: function name]                
    throw (BfErr)
  {
    // P   preparations
    if (!havesyms) {
      mysymbols(getargv0());
      havesyms = true;
    }
    // 1 create C++ file implementing the bodiesfunc function
    const size_t FNAME_SIZE=256;
    char ffile[FNAME_SIZE], _func[FNAME_SIZE];
    const char*ffunc;
    if(funcn && funcn[0])
      ffunc = funcn;
    else {
      SNprintf(_func,FNAME_SIZE,"%s%d",fname,function++);
      ffunc = _func;
    }
    DebugInfo(debug_depth,
	      "bodiesfunc::bodiesfunc(): must make function\n"
	      "      base name = %s\n"
	      "      func name = %s\n",fname,ffunc);
    SNprintf(ffile,FNAME_SIZE,"/tmp/%s.cc",fname);
    std::ofstream file(ffile);
    if(!file) 
      throw BfErr(message("cannot create temporary file \"%s\"\n",ffile));
    file  <<"//\n"
	  <<"// file "<<ffile<<" generated by make_func()\n"
	  <<"//\n"
	  <<"#include <cmath>\n"
	  <<"#include <body.h>\n"
	  <<"\n"
	  <<"using namespace falcON;\n"
	  <<"\n"
	  <<"#undef BD_TEST\n"
	  <<"#define bodies_func\n"
	  <<"#include <public/bodyfuncdefs.h>\n"
	  <<"\n"
	  <<"namespace {\n";
    try {
      for(int s=sub-1; s>0; --s)
	make_sub(file,s);
    } catch(ParseErr E) {
      throw BfErr(message("parse error: %s",text(E)));
    }
    file  <<"}\n"
	  <<"\n"
	  <<"\n"
	  <<"extern \"C\"{\n"
	  <<"  "<<stype[0]<<" "<<ffunc
	  <<"(bodies const&B, double const&t, const real*__P) {\n";
    for(int s=sub-1; s>0; --s)
      file<<"    "<<sname[s]<<" = "<<sname[s]<<"F(B,t,__P);\n";
    file  <<"\n"
	  <<"    return "<<sexpr[0]<<";\n"
	  <<"  }\n"
	  <<"}\n";
    file.close();
    // 2 compile the C++ file and create a shared object file
    compile(OPTFLAGS,fname);
    // 3 load the .so file and find our function and return it
    SNprintf(ffile,FNAME_SIZE,"/tmp/%s.so",fname);
    loadobj(ffile);
    Bf_pter func = (Bf_pter)findfn(const_cast<char*>(ffunc));
    if(func == 0)
      throw BfErr(message("findfn couldn't find \"%s\"\n",ffunc));
    return func;
  } // make_func()
} // namespace {
////////////////////////////////////////////////////////////////////////////////
//                                                                              
// implementing falcON::bodyfunc::bodyfunc()                                    
//                                                                              
////////////////////////////////////////////////////////////////////////////////
bodyfunc::bodyfunc(const char*oexpr) throw(falcON::exception)
  : FUNC(0), TYPE(0), EXPR(0), NPAR(0), NEED(fieldset::o)
{
  if(oexpr == 0 || *oexpr == 0) return;
  size_t len = strlen(oexpr)+1;
  EXPR = falcON_NEW(char,len);
  strncpy(EXPR,oexpr,len);
  // 0 eliminate white space from expression
  shrink(nexpr,MAX_LENGTH_EXPR,oexpr);
  if(*nexpr == 0) return;
  // 1 employ database
  const size_t FNAME_SIZE = 256;
  char fname[FNAME_SIZE],ffunc[FNAME_SIZE],ftype[MAX_TYPE_LENGTH];
  const char *funcname;
  BF_database*BD = 0;
  try {
    BD = new BF_database("bodyfunc","BFNAMES");
    DebugInfo(debug_depth,"bodyfunc::bodyfunc(): looking up database\n");
    // 1.1 try to find function in database
    funcname = BD->findfunc(nexpr,TYPE,NPAR,NEED);
    if(funcname) {                                  // found one!               
      DebugInfo(debug_depth,"bodyfunc::bodyfunc(): found one: %s\n",funcname);
      char ffile[FNAME_SIZE];
      SNprintf(ffile,FNAME_SIZE,"%s/%s.so",BD->directory(),funcname);
      loadobj(ffile);
      FUNC = (bf_pter)findfn(const_cast<char*>(funcname));
      if(FUNC) return;                              // SUCCESS !!!              
      DebugInfo(debug_depth,
		"bodyfunc::bodyfunc(): couldn't find %s in %s/%s.so\n",
		funcname,BD->directory(),funcname);
    }
    // 1.2 function not found, so try to get unique new function name
    SNprintf(fname,FNAME_SIZE,"bf_%s%d",RunInfo::pid(),function++);
    int fcount = BD->counter();                     // try to get valid counter 
    SNprintf(ffunc,FNAME_SIZE,"bf__%d",fcount);
    funcname = ffunc;
  }
  // 1.E database error occured, we cannot use it at all
  catch(DataBaseErr E) {
    if(BD) falcON_DEL_O(BD);
    funcname = 0;
    BD = 0;
    DebugInfo(debug_depth,
	      "bodyfunc::bodyfunc(): database problems: %s,",text(E));
  }
  // 2 function was not found in database, so must try to make it
  try {
    char Pexpr[MAX_LENGTH_EXPR];
    {
      const char*rexpr=nexpr;
      char*pexpr=Pexpr;
      NPAR = 0;
      full_parse(rexpr,pexpr,Pexpr+MAX_LENGTH_EXPR,NPAR);
      *pexpr = 0;
    }
    get_type(ftype,NEED,Pexpr);
    TYPE = ftype[0];
    FUNC = make_func(Pexpr,ftype,fname,funcname);
  }
  catch(ParseErr E) {
    if(BD) falcON_DEL_O(BD);
    throw exception("bodyfunc::bodyfunc(): parse error: %s",text(E));
  }
  catch(BfErr E) {
    if(BD) falcON_DEL_O(BD);
    delete_files(fname);
    throw exception("bodyfunc::bodyfunc(): %s",text(E));
  }
  catch(exception E) {
    if(BD) falcON_DEL_O(BD);
    delete_files(fname);
    throw E;
  }
  // 3 if database not faulty, try to put new function into it
  if(BD) {
    if(FUNC != 0 && funcname) {
      try {
	BD->put(fname,funcname,nexpr,TYPE,NPAR,NEED);
      } catch(DataBaseErr E) {
	DebugInfo(debug_depth,"database problems: %s,",text(E));
      }
    }
    falcON_DEL_O(BD);
  }
  // 4 delete temporary files
  delete_files(fname);
}
////////////////////////////////////////////////////////////////////////////////
bool bodyfunc::print_db(std::ostream&out)
{
  try {
    BF_database BD("bodyfunc","BFNAMES");
    return BD.printinfo(out);
  } catch(DataBaseErr E) {
    return false;
  }
}
////////////////////////////////////////////////////////////////////////////////
//                                                                              
// implementing falcON::Bodyfunc::Bodyfunc()                                    
//                                                                              
////////////////////////////////////////////////////////////////////////////////
falcON::Bodyfunc::Bodyfunc(const char*expr, const char*pars)
  throw(falcON::exception)
  : bodyfunc(expr), PARS(0)
{
  if(is_empty()) return;
  if(pars) {
    size_t len = strlen(pars)+1;
    PARS = falcON_NEW(char,len);
    strncpy(PARS,pars,len);
  }
  if(NPAR) {
    if(pars == 0)
      throw exception("Bodyfunc::Bodyfunc(): "
		      "expression \"%s\" requires %d parameters, "
		      "but none are given", expr,NPAR);
    int n = 
#ifdef falcON_REAL_IS_FLOAT
      nemoinpf
#else
      nemoinpd
#endif
      (const_cast<char*>(pars),P,MAXPAR);
    if(n < NPAR)
      throw exception("Bodyfunc::Bodyfunc(): "
		      "expression \"%s\" requires %d parameters, "
		      "but only %d are given", expr,NPAR,n);
    if(n > NPAR)
      falcON_Warning("Bodyfunc::Bodyfunc(): "
		     "expression \"%s\" requires %d parameters, "
		     "but %d are given; will ignore last %d",
		     expr,NPAR,n,n-NPAR);
  }
}
//------------------------------------------------------------------------------
falcON::Bodyfunc::Bodyfunc(const char*expr, const real*pars, int npar)
  throw(falcON::exception)
  : bodyfunc(expr), PARS(0)
{
  if(is_empty()) return;
  if(NPAR) {
    if(npar == 0 || pars == 0)
      throw exception("Bodyfunc::Bodyfunc(): "
		      "expression \"%s\" requires %d parameters, "
		      "but none are given", expr,NPAR);
    if(npar < NPAR)
      throw exception("Bodyfunc::Bodyfunc(): "
		      "expression \"%s\" requires %d parameters, "
		      "but only %d are given", expr,NPAR,npar);
    if(npar > NPAR)
      falcON_Warning("Bodyfunc::Bodyfunc(): "
		     "expression \"%s\" requires %d parameters, "
		     "but %d are given; will ignore last %d",
		     expr,NPAR,npar,npar-NPAR);
    if(npar > 0) {
      int _len = npar*16;
      PARS = falcON_NEW(char,_len);
      char par[64], *PA=PARS;
      for(int ipar=0; ipar!=npar; ++ipar) {
	P[ipar] = pars[ipar];
	SNprintf(par,64,"%f",pars[ipar]);
	strncpy(PA,par,_len);
	_len -= strlen(par)+1;
	if(_len < 0) falcON_THROW("Bodyfunc::Bodyfunc: "
				  "difficulty parsing parameters\n");
	strncat(PA,",",1);
	PA += strlen(par) + 1;
      }
    }
  }
}
////////////////////////////////////////////////////////////////////////////////
//                                                                              
// implementing falcON::BodyFunc<T>::BodyFunc()                                 
//                                                                              
////////////////////////////////////////////////////////////////////////////////
namespace {
  inline const char* Typeof(char T) {
    switch(T) {
    case 'b' : return "bool";
    case 'i' : return "int";
    case 'r' : return nameof(falcON::real);
    case 'v' : return nameof(falcON::vect);
    default  : return "unknown type";
    }
  }
}
//------------------------------------------------------------------------------
template<typename T>
falcON::BodyFunc<T>::BodyFunc(const char*expr, const char*pars)
  throw(falcON::exception)
  : Bodyfunc(expr,pars)
{
  if(is_empty()) return;
  if(TYPE != bf_type<T>::type )
    throw exception("BodyFunc<%s>::BodyFunc(): expression \"%s\" is of type %s",
		    nameof(T),expr,Typeof(TYPE));
}
template falcON::BodyFunc<bool>::BodyFunc(const char*,const char*)
  throw(falcON::exception);
template falcON::BodyFunc<int >::BodyFunc(const char*,const char*)
  throw(falcON::exception);
template falcON::BodyFunc<falcON::real>::BodyFunc(const char*,const char*)
  throw(falcON::exception);
template falcON::BodyFunc<falcON::vect>::BodyFunc(const char*,const char*)
  throw(falcON::exception);
//------------------------------------------------------------------------------
template<typename T>
falcON::BodyFunc<T>::BodyFunc(const char*expr, const real*pars, int npar)
  throw(falcON::exception)
  : Bodyfunc(expr,pars,npar)
{
  if(is_empty()) return;
  if(TYPE != bf_type<T>::type)
    throw exception("BodyFunc<%s>::BodyFunc(): expression \"%s\" is of type %s",
		    nameof(T),expr,Typeof(TYPE));
}
template
falcON::BodyFunc<bool>::BodyFunc(const char*,const falcON::real*,int)
  throw(falcON::exception);
template
falcON::BodyFunc<int >::BodyFunc(const char*,const falcON::real*,int)
  throw(falcON::exception);
template
falcON::BodyFunc<falcON::real>::BodyFunc(const char*,const falcON::real*,int)
  throw(falcON::exception);
template
falcON::BodyFunc<falcON::vect>::BodyFunc(const char*,const falcON::real*,int)
  throw(falcON::exception);
////////////////////////////////////////////////////////////////////////////////
//                                                                              
// implementing falcON::BodyFilter::BodyFilter                                  
//                                                                              
////////////////////////////////////////////////////////////////////////////////
falcON::BodyFilter::BodyFilter(const char*expr, const char*pars)
  throw(falcON::exception)
  : BodyFunc<bool>(expr,pars), TIME(0.) {}
////////////////////////////////////////////////////////////////////////////////
falcON::BodyFilter::BodyFilter(const char*expr, const real*pars, int npar)
  throw(falcON::exception)
  : BodyFunc<bool>(expr,pars,npar), TIME(0.) {}
////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// implementing falcON::bodiesfunc::bodiesfunc()                              //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
bodiesfunc::bodiesfunc(const char*oexpr) throw(falcON::exception)
{
  // 0 eliminate white space from expression
  shrink(nexpr,MAX_LENGTH_EXPR,oexpr);
  // 1 employ database
  const size_t FNAME_SIZE=256;
  char fname[FNAME_SIZE],ffunc[FNAME_SIZE];
  const char *funcname;
  BF_database*BD = 0;
  try {
    BD = new BF_database("bodiesfunc","BFNAMES");
    DebugInfo(debug_depth,"bodiesfunc::bodiesfunc(): looking up database\n");
    // 1.1 try to find function in database
    funcname = BD->findfunc(nexpr,TYPE,NPAR,NEED);
    if(funcname) {                                  // found one!               
      DebugInfo(debug_depth,
		"bodiesfunc::bodiesfunc(): found one: %s\n",funcname);
      char ffile[FNAME_SIZE];
      SNprintf(ffile,FNAME_SIZE,"%s/%s.so",BD->directory(),funcname);
      loadobj(ffile);
      FUNC = (Bf_pter)findfn(const_cast<char*>(funcname));
      if(FUNC) return;                            // SUCCESS !!!              
      DebugInfo(debug_depth,
		"bodiesfunc::bodiesfunc(): couldn't find %s in %s/%s.so\n",
		funcname,BD->directory(),funcname);
    }
    // 1.2 function not found, so try to get unique new function name
    SNprintf(fname,FNAME_SIZE,"Bf_%s%d",RunInfo::pid(),function++);
    int fcount = BD->counter();                     // try to get valid counter 
    SNprintf(ffunc,FNAME_SIZE,"Bf__%d",fcount);
    funcname = ffunc;
  }
  // 1.E database error occured, we cannot use it at all
  catch(DataBaseErr E) {
    if(BD) falcON_DEL_O(BD);
    funcname = 0;
    BD = 0;
    DebugInfo(debug_depth,
	      "bodiesfunc::bodiesfunc(): database problems: %s,",text(E));
  }
  // 2 function was not found in database, so must try to make it
  try {
    // 2.1 parse expression
    parse_expr(nexpr);
    if(debug(debug_depth)) {
      std::cerr<<" sub = "<<sub<<"  par = "<<par<<"\n"
	       <<" sname[0] = \""<<sname[0]<<"\""
	       <<" sexpr[0] = \""<<sexpr[0]<<"\"\n";
      for(int s=1; s!=sub; ++s)
	std::cerr<<" sname["<<s<<"] = \""<<sname[s]<<"\""
		 <<" soper["<<s<<"] = \""<<OpName[soper[s]]<<"\""
		 <<" scond["<<s<<"] = \""<<(scond[s]? scond[s]:"empty")<<"\""
		 <<" sexpr["<<s<<"] = \""<<(sexpr[s]? sexpr[s]:"empty")<<"\"\n";
    }
    NPAR = par;
    // 2.2 obtain type and need information
    get_types(NEED);
    if(debug(debug_depth)) {
      std::cerr<<" need     = \""<<NEED<<"\"\n";
      for(int s=0; s!=sub; ++s)
	std::cerr<<" stype["<<s<<"] = \""<<stype[s]<<"\"\n";
    }
    TYPE = stype[0][0];
    // 2.3 generate function and obtain pointer
    FUNC = make_func(fname,funcname);
  }
  // 2.E catch errors in making new function
  catch(ParseErr E) {
    if(BD) falcON_DEL_O(BD);
    throw exception("bodiesfunc::bodiesfunc(): parse error: %s",text(E));
  }
  catch(BfErr E) {
    if(BD) falcON_DEL_O(BD);
    delete_files(fname);
    throw exception("bodiesfunc::bodiesfunc(): %s",text(E));
  }
  catch(exception E) {
    if(BD) falcON_DEL_O(BD);
    delete_files(fname);
    throw E;
  }
  // 3 if database not faulty, try to put new function into it
  if(BD) {
    if(FUNC != 0 && funcname) {
      try {
	BD->put(fname,funcname,nexpr,TYPE,NPAR,NEED);
      } catch(DataBaseErr E) {
	DebugInfo(debug_depth,"database problems: %s,",text(E));
      }
    }
    falcON_DEL_O(BD);
  }
  // 4 delete temporary files
  delete_files(fname);
}
////////////////////////////////////////////////////////////////////////////////
bool bodiesfunc::print_db(std::ostream&out)
{
  try {
    BF_database BD("bodiesfunc","BFNAMES");
    return BD.printinfo(out);
  } catch(DataBaseErr E) {
    return false;
  }
}
////////////////////////////////////////////////////////////////////////////////
#ifdef falcON_PROPER
////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// simple bodiesmethods                                                       //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
namespace {
  using falcON::real;
  using falcON::vect;
  using namespace falcON;
  //----------------------------------------------------------------------------
  void Bm_m(void*X, bodies const&B, double const&, const real*) {
    LoopAllBodies(&B,b)
      static_cast<real*>(X)[bodyindex(b)] = mass(b);
  }
  //----------------------------------------------------------------------------
  void Bm_pos(void*X, bodies const&B, double const&, const real*) {
    LoopAllBodies(&B,b)
      static_cast<vect*>(X)[bodyindex(b)] = pos(b);
  }
  //----------------------------------------------------------------------------
  void Bm_x(void*X, bodies const&B, double const&, const real*) {
    LoopAllBodies(&B,b)
      static_cast<real*>(X)[bodyindex(b)] = pos(b)[0];
  }
  //----------------------------------------------------------------------------
  void Bm_y(void*X, bodies const&B, double const&, const real*) {
    LoopAllBodies(&B,b)
      static_cast<real*>(X)[bodyindex(b)] = pos(b)[1];
  }
  //----------------------------------------------------------------------------
  void Bm_z(void*X, bodies const&B, double const&, const real*) {
    LoopAllBodies(&B,b)
      static_cast<real*>(X)[bodyindex(b)] = pos(b)[2];
  }
  //----------------------------------------------------------------------------
  void Bm_r(void*X, bodies const&B, double const&, const real*) {
    LoopAllBodies(&B,b)
      static_cast<real*>(X)[bodyindex(b)] = abs(pos(b));
  }
  //----------------------------------------------------------------------------
  void Bm_R(void*X, bodies const&B, double const&, const real*) {
    LoopAllBodies(&B,b)
      static_cast<real*>(X)[bodyindex(b)] = 
      sqrt(square(pos(b)[0])+square(pos(b)[1]));
  }
  //----------------------------------------------------------------------------
  void Bm_vel(void*X, bodies const&B, double const&, const real*) {
    LoopAllBodies(&B,b)
      static_cast<vect*>(X)[bodyindex(b)] = vel(b);
  }
  //----------------------------------------------------------------------------
  void Bm_vx(void*X, bodies const&B, double const&, const real*) {
    LoopAllBodies(&B,b)
      static_cast<real*>(X)[bodyindex(b)] = vel(b)[0];
  }
  //----------------------------------------------------------------------------
  void Bm_vy(void*X, bodies const&B, double const&, const real*) {
    LoopAllBodies(&B,b)
      static_cast<real*>(X)[bodyindex(b)] = vel(b)[1];
  }
  //----------------------------------------------------------------------------
  void Bm_vz(void*X, bodies const&B, double const&, const real*) {
    LoopAllBodies(&B,b)
      static_cast<real*>(X)[bodyindex(b)] = vel(b)[2];
  }
  //----------------------------------------------------------------------------
  void Bm_v(void*X, bodies const&B, double const&, const real*) {
    LoopAllBodies(&B,b)
      static_cast<real*>(X)[bodyindex(b)] = abs(vel(b));
  }

  //----------------------------------------------------------------------------
  void Bm_acc(void*X, bodies const&B, double const&, const real*) {
    LoopAllBodies(&B,b)
      static_cast<vect*>(X)[bodyindex(b)] = acc(b);
  }
  //----------------------------------------------------------------------------
  void Bm_ax(void*X, bodies const&B, double const&, const real*) {
    LoopAllBodies(&B,b)
      static_cast<real*>(X)[bodyindex(b)] = acc(b)[0];
  }
  //----------------------------------------------------------------------------
  void Bm_ay(void*X, bodies const&B, double const&, const real*) {
    LoopAllBodies(&B,b)
      static_cast<real*>(X)[bodyindex(b)] = acc(b)[1];
  }
  //----------------------------------------------------------------------------
  void Bm_az(void*X, bodies const&B, double const&, const real*) {
    LoopAllBodies(&B,b)
      static_cast<real*>(X)[bodyindex(b)] = acc(b)[2];
  }
  //----------------------------------------------------------------------------
  void Bm_a(void*X, bodies const&B, double const&, const real*) {
    LoopAllBodies(&B,b)
      static_cast<real*>(X)[bodyindex(b)] = abs(acc(b));
  }
  //----------------------------------------------------------------------------
  void Bm_l(void*X, bodies const&B, double const&, const real*) {
    LoopAllBodies(&B,b)
      static_cast<vect*>(X)[bodyindex(b)] = pos(b) ^ vel(b);
  }
  //----------------------------------------------------------------------------
  void Bm_lx(void*X, bodies const&B, double const&, const real*) {
    LoopAllBodies(&B,b)
      static_cast<real*>(X)[bodyindex(b)] = 
      pos(b)[1]*vel(b)[2] - pos(b)[2]*vel(b)[1];
  }
  //----------------------------------------------------------------------------
  void Bm_ly(void*X, bodies const&B, double const&, const real*) {
    LoopAllBodies(&B,b)
      static_cast<real*>(X)[bodyindex(b)] = 
      pos(b)[2]*vel(b)[0] - pos(b)[0]*vel(b)[2];
  }
  //----------------------------------------------------------------------------
  void Bm_lz(void*X, bodies const&B, double const&, const real*) {
    LoopAllBodies(&B,b)
      static_cast<real*>(X)[bodyindex(b)] = 
      pos(b)[0]*vel(b)[1] - pos(b)[1]*vel(b)[0];
  }
  //----------------------------------------------------------------------------
  void Bm_ltot(void*X, bodies const&B, double const&, const real*) {
    LoopAllBodies(&B,b)
      static_cast<real*>(X)[bodyindex(b)] = abs(pos(b)^vel(b));
  }
  //----------------------------------------------------------------------------
  const int  N_BM=32;
  Bm_pter    F_BM[N_BM] = {&Bm_m,                                    // 1: 1
			   &Bm_pos,&Bm_x,&Bm_y,&Bm_z,&Bm_r,&Bm_R,    // 6: 2-7
			   &Bm_vel,&Bm_vx,&Bm_vy,&Bm_vz,&Bm_v,       // 5: 8-12
			   &Bm_acc,&Bm_ax,&Bm_ay,&Bm_az,&Bm_a,       // 5: 13-17
			   &Bm_l,&Bm_lx,&Bm_ly,&Bm_lz,&Bm_ltot,0};   // 5: 18-22
  char       T_BM[N_BM] = "rvrrrrrvrrrrvrrrrvrrrr";
  fieldset   I_BM[N_BM] = {fieldset::m,
			   fieldset::x,fieldset::x,fieldset::x,fieldset::x,
			   fieldset::x,fieldset::x,
			   fieldset::v,fieldset::v,fieldset::v,fieldset::v,
			   fieldset::v,
			   fieldset::a,fieldset::a,fieldset::a,fieldset::a,
			   fieldset::a,
			   fieldset::phases,fieldset::phases,
			   fieldset::phases,fieldset::phases,
			   fieldset::phases};
  const char*E_BM[N_BM] = {"m",
			   "pos","x","y","z","r","R",
			   "vel","vx","vy","vz","v",
			   "acc","ax","ay","az","a",
			   "l","lx","ly","lz","ltot",0};
}
////////////////////////////////////////////////////////////////////////////////
//                                                                              
// implementing falcON::bodiesmethod::bodiesmethod()                            
//                                                                              
////////////////////////////////////////////////////////////////////////////////
falcON::bodiesmethod::bodiesmethod(const char  *oexpr) falcON_THROWING
{
  // 0 eliminate white space from expression
  shrink(nexpr,MAX_LENGTH_EXPR,oexpr);
  // 1 check against simple expressions
  DebugInfo(debug_depth,"bodiesmethod::bodiesmethod(): "
	    "checking for basic expression\n");
  for(int n=0; F_BM[n]; ++n) {
    if(0==strcmp(nexpr,E_BM[n])) {
      FUNC = F_BM[n];
      TYPE = T_BM[n];
      NPAR = 0;
      NEED = I_BM[n];
      return;
    }
  }
  // 2 employ database
  const size_t FNAME_SIZE=256;
  char fname[FNAME_SIZE],ffunc[FNAME_SIZE],ftype[MAX_TYPE_LENGTH];
  const char *funcname;
  BF_database*BD = 0;
  try {
    BD = new BF_database("bodiesmethod","BFNAMES");
    DebugInfo(debug_depth,
	      "bodiesmethod::bodiesmethod(): looking up database\n");
    // 2.1 try to find function in database
    funcname = BD->findfunc(nexpr,TYPE,NPAR,NEED);
    if(funcname) {                                  // found one!               
      DebugInfo(debug_depth,
		"bodiesmethod::bodiesmethod(): found one: %s\n",funcname);
      char ffile[FNAME_SIZE];
      SNprintf(ffile,FNAME_SIZE,"%s/%s.so",BD->directory(),funcname);
      loadobj(ffile);
      FUNC = (Bm_pter)findfn(const_cast<char*>(funcname));
      if(FUNC) return;                              // SUCCESS !!!              
      DebugInfo(debug_depth,"bodiesmethod::bodiesmethod(): "
		"couldn't find %s in %s/%s.so\n",
		funcname,BD->directory(),funcname);
    }
    // 2.2 function not found, so try to get unique new function name
    SNprintf(fname,FNAME_SIZE,"Bm_%s%d",RunInfo::pid(),function++);
    int fcount = BD->counter();                     // try to get valid counter 
    SNprintf(ffunc,FNAME_SIZE,"Bm__%d",fcount);
    funcname = ffunc;
  }
  // 2.E database error occured, we cannot use it at all
  catch(DataBaseErr E) {
    if(BD) falcON_DEL_O(BD);
    funcname = 0;
    BD = 0;
    DebugInfo(debug_depth,"bodiesmethod::bodiesmethod(): "
	      "database problems: %s,",text(E));
  }
  // 3 function was not found in database, so must try to make it
  try {
    char Pexpr[MAX_LENGTH_EXPR];
    {
      const char*rexpr=nexpr;
      char*pexpr=Pexpr;
      NPAR = 0;
      full_parse(rexpr,pexpr,Pexpr+MAX_LENGTH_EXPR,NPAR);
      *pexpr = 0;
    }
    get_type(ftype,NEED,Pexpr);
    TYPE = ftype[0];
    FUNC = make_method(Pexpr,ftype,fname,funcname);
  }
  catch(ParseErr E) {
    if(BD) falcON_DEL_O(BD);
    falcON_THROW("bodiesmethod::bodiesmethod(): parse error: %s",text(E));
  }
  catch(BfErr E) {
    if(BD) falcON_DEL_O(BD);
    delete_files(fname);
    falcON_THROW("bodiesmethod::bodiesmethod(): %s",text(E));
  }
  catch(exception E) {
    if(BD) falcON_DEL_O(BD);
    delete_files(fname);
    throw E;
  }
  // 3 if database not faulty, try to put new function into it
  if(BD) {
    if(FUNC != 0 && funcname) {
      try {
	BD->put(fname,funcname,nexpr,TYPE,NPAR,NEED);
      } catch(DataBaseErr E) {
	DebugInfo(debug_depth,"database problems: %s,",text(E));
      }
    }
    falcON_DEL_O(BD);
  }
  // 4 delete temporary files
  delete_files(fname);
}
#endif // falcON_PROPER
////////////////////////////////////////////////////////////////////////////////
#endif // falcON_NEMO

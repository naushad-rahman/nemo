/*
 *  preprocessor to run (a series of) CGS
 *
 *
 *	3-nov-2005		written
 *     12-dec-2005     added writing pos/vel files with freqout=
 */

#include <stdinc.h>
#include <getparam.h>

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

string defv[] = {
    "out=???\n       directory in which GALAXY will be run",
    "nbody=40000\n   nbody, if needed",
    "nrad=80\n       nradii in code",
    "maxstep=1000\n  max number of integration steps",
    "dt=0.0025\n     initial integration step",
    "tstart=0.0\n    start time",
    "tstop=4.0\n     stop time",
    "dtmin=0.001\n   min integration time",
    "dtmax=0.01\n    max integration time",
    "flag=1\n        data creation flag",
    "mass=1.0\n      total mass of system",
    "virial=1.0\n    initial virial ratio if flag=2",
    "freqcmss=2000\n freq of center of mass adjustments",
    "freqdiag=50\n   freq",
    "freqout=10\n    freq of output of snapshots",
    "exe=CGS.exe\n   name of CGS executable",
    "in=\n           optional input snapshot (nemo format)",
    "nemo=t\n        convert data to NEMO and cleanup ASCII",
    "VERSION=0.3\n   12-dec-05 PJT",
    NULL,
};

string usage = "NEMO frontend to run CGS";

string cvsid="$Id$";

int nemo_main()
{
  int i, nbody, flag;
  real scale, dt, dtout, dtlog, tstop; 
  real virial = getrparam("virial");
  bool Qnemo = getbparam("nemo");
  string exefile = getparam("exe");
  string rundir = getparam("out");
  string infile;
  stream datstr;
  char fullname[256];
  char command[256];
  char line[256];
  

  make_rundir(rundir);

  if (hasvalue("in")) {
    infile = getparam("in");
    flag = 3;
    sprintf(command,"snapprint %s x,y,z    > %s/%s",infile,rundir,"initPOS.dat");
    dprintf(0,">>> %s\n",command);
    system(command);
    sprintf(command,"snapprint %s vx,vy,vz > %s/%s",infile,rundir,"initVEL.dat");
    dprintf(0,">>> %s\n",command);
    system(command);
    sprintf(command,"cat %s/%s | wc -l",rundir,"initVEL.dat");
    datstr = popen(command,"r");
    if (fgets(line,256,datstr) == 0) {
      warning("bad read finding nbody, trying keyword");
      nbody = getiparam("nbody");
    } else {
      line[strlen(line)-1] = 0;
      nbody = atoi(line);
    }
    dprintf(0,"line=%s\n",line);
    dprintf(0,"Using snapshot in=%s with nbody=%d\n",infile,nbody);
  } else {
    infile = 0;
    flag = getiparam("flag");
    nbody = getiparam("nbody");
  }

  if (flag == 2) {
    sprintf(fullname,"%s/%s",rundir,"initial_virial.dat");    
    datstr = stropen(fullname,"w");    
    fprintf(datstr,"%g\n",virial);
    strclose(datstr);
  }

  sprintf(fullname,"%s/%s",rundir,"PARAMETER.DAT");
  datstr = stropen(fullname,"w");    
  fprintf(datstr,"%d\n",getiparam("nrad"));
  fprintf(datstr,"%d\n",nbody);
  fprintf(datstr,"%d\n",getiparam("maxstep"));
  fprintf(datstr,"%d\n",getiparam("freqcmss"));
  fprintf(datstr,"%d\n",getiparam("freqdiag"));
  fprintf(datstr,"%d\n",getiparam("freqout"));
  fprintf(datstr,"%g\n",getdparam("dt"));
  fprintf(datstr,"%g\n",getdparam("tstart"));
  fprintf(datstr,"%g\n",getdparam("tstop"));
  fprintf(datstr,"%g\n",getdparam("mass"));
  fprintf(datstr,"%d\n",flag);
  fprintf(datstr,"%g\n",getdparam("dtmax"));
  fprintf(datstr,"%g\n",getdparam("dtmin"));
  strclose(datstr);

  goto_rundir(rundir);

  sprintf(command,"%s >& CGS.log",exefile);
  dprintf(0,">>> %s\n",command);
  system(command);

  if (Qnemo) {
    sprintf(command,"tabtos fort.90 snap.out nbody,time skip,pos,vel; rm fort.90");
    dprintf(0,">>> %s\n",command);
    system(command);
  }
  
}


goto_rundir(string name)
{
    if (chdir(name))
        error("Cannot change directory to %s",name);
}

make_rundir(string name)
{
    if (mkdir(name, 0755))
        error("Run directory %s already exists",name);
}

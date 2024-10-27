#!/bin/bash

helpFunction()
{
   echo ""
   echo "Usage: $0 -e environment -b benchmark_type -a anchor_install_path -p pmdk_path"
   echo "-e  Environment to setup and run [native|scone]"
   echo "-b  Benchmark type [benchmarks|microbenchmarks|write_amplification|breakdown]"
   echo "-a  Folder where Anchor is -- will be installed in */build - Absolute path"
   echo "-p  Folder where custom pmdk is -- will be installed in */build - Absolute path"
   exit 1 # Exit script after printing help
}

# Absolute path to this script, e.g. /home/user/bin/foo.sh
script=$(readlink -f "$0")
script_path=$(dirname "$script")

while getopts "e:b:a:p:" opt
do
   case "$opt" in
      e ) env="$OPTARG" ;;
      b ) benchmark="$OPTARG" ;;
      a ) anchor_path="$OPTARG" ;;
      p ) pmdk_path="$OPTARG" ;;
      ? ) helpFunction ;; # Print helpFunction in case parameter is non-existent
   esac
done

# Print helpFunction in case parameters are empty
if [ -z "$env" ] || [ -z "$benchmark" ]
then
   echo "Environment and benchmark parameters are mandatory";
   helpFunction
fi

## Args parsing + build flags define
stats=""
scone=""
write_ampl=""
#native case
if [ "$env" = "native" ]
then
    if [ -z "$anchor_path" ]
    then
        echo "Anchor path parameter is mandatory in native case";
        helpFunction
    fi

    if [ "$benchmark" = "benchmarks" ]
    then
        if [ -z "$pmdk_path" ]
        then
            echo "PMDK path parameter is mandatory in native case regular benchmarks"
            helpFunction
        fi
    elif [ "$benchmark" = "write_amplification" ]
    then
        stats="STATS"
        write_ampl="WRITE_AMPL"
    elif [ "$benchmark" = "breakdown" ]
    then
        stats="STATS"
    else
        echo "Invalid benchmark choice for native. Options : [benchmarks|write_amplification|breakdown]"
        helpFunction
    fi
fi

#scone case
if [ "$env" = "scone" ]
then
    if [ -z "$anchor_path" ]
    then
        echo "Anchor path parameter is mandatory in scone case";
        helpFunction
    fi
    if [ ! "$benchmark" = "benchmarks" ] && [ ! "$benchmark" = "microbenchmarks" ]
    then
        echo "Invalid benchmark choice for scone. Options : [benchmarks|microbenchmarks]"
        helpFunction
    fi
    scone="SCONE"
fi

#check that install directory exists
if [ ! -d "$anchor_path" ]
then
  echo "Anchor install directory does not exist"
  helpFunction
fi
if [ ! -z "$pmdk_path" ] && [ ! -d "$pmdk_path" ]
then
    echo "PMDK directory does not exist"
    helpFunction
fi

#install the libraries
if [ ! -z "$pmdk_path" ]
then
    echo "Installing custom PMDK"
    pushd ${pmdk_path} > /dev/null
    #echo "make clean && make && make install prefix=${pmdk_path}/build"
    make clean > /dev/null
    make > /dev/null
    mkdir -p ${pmdk_path}/build
    make install prefix=${pmdk_path}/build > /dev/null
    popd > /dev/null
fi

echo "Installing Anchor libraries"
echo "${script_path}/library_version_setup.py "$anchor_path" "$stats" "$scone" "$write_ampl" "
python ${script_path}/library_version_setup.py "$anchor_path" "$stats" "$scone" "$write_ampl" > /dev/null

#perform replacements of the appropriate internal config files
cfg_folder="${script_path}/experiments/run_experiments/"
cfg_file=${cfg_folder}"run_${benchmark}_${env}.yml"

git checkout ${cfg_file}
sed -i "s|pmdk_build_path|$pmdk_path|g" "${cfg_file}"
sed -i "s|pmdk_path|$pmdk_path|g" "${cfg_file}"
sed -i "s|anchor_build_path|$anchor_path|g" "${cfg_file}"
sed -i "s|anchor_path|$script_path|g" "${cfg_file}"

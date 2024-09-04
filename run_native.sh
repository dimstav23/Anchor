#!/bin/sh

helpFunction()
{
   echo ""
   echo "Usage: $0 -e environment -b benchmark_type -a anchor_install_path -p pmdk_path"
   echo "-e  Environment to setup and run [native]"
   echo "-b  Benchmark type [benchmarks|write_amplification|breakdown]"
   echo "-a  Folder where Anchor root directory is - Absolute path"
   echo "-p  Folder where custom pmdk directory is - Absolute path"
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

#perform replacements of the appropriate internal config files
cfg_folder="${script_path}/experiments/run_experiments/"
cfg_file=${cfg_folder}"run_${benchmark}_${env}.yml"

git checkout ${cfg_file}
sed -i "s|pmdk_build_path|$pmdk_path|g" "${cfg_file}"
sed -i "s|pmdk_path|$pmdk_path|g" "${cfg_file}"
sed -i "s|anchor_build_path|$anchor_path|g" "${cfg_file}"
sed -i "s|anchor_path|$script_path|g" "${cfg_file}"

#run the benchmarks --- repeats parameter is embedded in python scripts
pushd ./experiments/run_experiments > /dev/null
if [ "$benchmark" = "benchmarks" ] || [ "$benchmark" = "breakdown" ]
then
    echo "python3 run_experiments.py ${cfg_file}"
    python3 run_experiments.py ${cfg_file} > /dev/null
else
    echo "python3 run_microbenchmarks.py ${cfg_file}"
    python3 run_microbenchmarks.py ${cfg_file} > /dev/null
fi
popd > /dev/null

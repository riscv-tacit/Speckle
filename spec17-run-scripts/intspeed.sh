#!/bin/bash

# Defaults
num_threads=1
counters=0

function usage
{
    echo "usage: intspeed.sh <benchmark-name> [-H | -h | --help] [--threads <int>] [--workload <int>]"
    echo "   benchmark-name: the spec17 run directory with binary and inputs"
    echo "   threads: number of OpenMP threads to use. Default: ${num_threads}"
    echo "   workload: which workload number to run. Leaving this unset runs all."
    echo "   counters: if set, runs an hpm_counters instance on each hart"
    echo "   trace: if set, runs the workload with trace enabled"
    echo "   dma: if set, runs the workload with DMA target enabled"
    echo "   lossy: if set, traces in lossy mode (Pause/Resume instead of stalling)"
}

if [ $# -eq 0 -o "$1" == "--help" -o "$1" == "-h" -o "$1" == "-H" ]; then
    usage
    exit 3
fi

bmark_name=$1
shift

while test $# -gt 0
do
   case "$1" in
        --workload)
            shift;
            workload_num=$1
            ;;
        --threads)
            shift;
            num_threads=$1
            ;;
        --counters)
            counters=1;
            ;;
        --trace)
            trace=1;
            ;;
        --dma)
            dma=1;
            ;;
        --lossy)
            lossy=1;
            ;;
        -h | -H | -help)
            usage
            exit
            ;;
        --*) echo "ERROR: bad option $1"
            usage
            exit 1
            ;;
        *) echo "ERROR: bad argument $1"
            usage
            exit 2
            ;;
    esac
    shift
done

work_dir=$PWD
export OMP_NUM_THREADS=$num_threads
mkdir -p ~/output

if [ -z "$workload_num" ]; then
    if [ -z "$trace" ]; then
        runscript="run.sh"
    elif [ -z "$dma" ]; then
        runscript="run_traced.sh"
        echo "Using TRACING!"
    elif [ -z "$lossy" ]; then
        runscript="run_traced_dma.sh"
        echo "Using TRACING WITH DMA TARGET!"
    else
        runscript="run_traced_dma_lossy.sh"
        echo "Using LOSSY TRACING WITH DMA TARGET!"
    fi
    echo "Starting speed $bmark_name run with $OMP_NUM_THREADS threads"
else
    if [ -z "$trace" ]; then
        runscript="run_workload${workload_num}.sh"
    elif [ -z "$dma" ]; then
        runscript="run_workload${workload_num}_traced.sh"
        echo "Using TRACING!"
    elif [ -z "$lossy" ]; then
        runscript="run_workload${workload_num}_traced_dma.sh"
        echo "Using TRACING WITH DMA TARGET!"
    else
        runscript="run_workload${workload_num}_traced_dma_lossy.sh"
        echo "Using LOSSY TRACING WITH DMA TARGET!"
    fi
    echo "Starting speed $bmark_name (workload ${workload_num}) run with $OMP_NUM_THREADS threads"
fi

# In some systems we might not support for our counter program; so optionally disable it 
if [ -z "$DISABLE_COUNTERS" -a "$counters" -ne 0 ]; then
    start_counters
fi

# Actually start the workload
cd $work_dir/${bmark_name}

if [ -z "$workload_num" ]; then
    full_name=${bmark_name}
else
    full_name=${bmark_name}_${workload_num}
fi

# busybox has a bug in time where escape characters (e.g. \n) are not
# interpreted correctly, we have to put the CSV header in manually
echo "name,RealTime,UserTime,KernelTime" >> ~/output/${full_name}.csv

/usr/bin/time -a -o ~/output/${full_name}.csv -f "${full_name},%e,%U,%S" \
    ./${runscript} > ~/output/${full_name}.out 2> ~/output/${full_name}.err
    # ./${runscript} 

if [ -z "$DISABLE_COUNTERS" -a "$counters" -ne 0 ]; then
    stop_counters
fi

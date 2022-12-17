for ((i = 1 ; i <= 15 ; i++))
do
    if (( $i<10));
    then
        echo "python3 in0${i}.py"
        eval $"python3 in0${i}.py"
    else
        echo "python3 in${i}.py"
        eval $"python3 in${i}.py"
    fi
done

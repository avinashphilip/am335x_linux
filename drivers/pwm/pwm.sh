export value
pth=/sys/bus/platform/devices
if [ $1 == --help ] || [ $# == 0 ] || [ $# == 1 ]
	then
		echo
		echo  USAGE
		echo  
		echo " ./pwm.sh <device> <parameter no> <val>"
		echo  
		echo "   <device> "
		echo ls $pth/pwm_test*
		echo  
		echo "   parameter number can be"
		echo "     '1' ->run,  	'2' ->duty,      '3' ->period"
		echo "     '4' ->config '5' ->polarity  '6' ->request"
		exit 0
fi

if [ $2 == 1 ]
	then
		par=run
elif [ $2 == 2 ]
	then
		par=duty
elif [ $2 == 3 ]
	then
		par=period
elif [ $2 == 4 ]
	then
		par=config
elif [ $2 == 5 ]
	then
		par=polarity
elif [ $2 == 6 ]
	then
		par=request
fi
#clear

if [ $? != 0 ] || [ $2 == 0 ]
	then
		value="--help"
fi
#echo "cat $pth/"$1/$par
cat $pth/$1/$par
if [ $# != 3 ]
	then
		exit 1
fi
value=$3
echo
#echo "echo" $value "> " $pth/$1/$par
echo $value > $pth/$1/$par
if [ $? != 0 ]
	then
#		echo  
		cat $pth/$1/$par
#		echo "echo" $value "> "$pth/$1/$par", Command FAILED!!"
#		echo "	Parameter NOT CHANGED."
	else
#		echo  
		cat $pth/$1/$par
#		echo "echo" $value "> " $pth/$1/$par", Command SUCCEDED..."
#		echo "	Parameter CHANGED."
fi
#echo
#echo "cat " $pth/$1/$par
#cat $pth/$1/$par

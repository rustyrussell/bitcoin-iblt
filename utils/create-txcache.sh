#! /bin/sh

../../bitcoin-iterate/bitcoin-iterate -q --start-hash=00000000000000001200ffc70af39882728895900a9e0430594c7e05f24c4887 --end-hash=000000000000000006b3d4088f4da31308b91d194792282f1eb9454de0a35f99 --tx="%th %tF %tX" | ./add-to-txcache

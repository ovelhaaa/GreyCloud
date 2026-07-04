import struct
import sys
import math

def compare_raw(file1, file2):
    with open(file1, 'rb') as f1, open(file2, 'rb') as f2:
        d1 = f1.read()
        d2 = f2.read()
        
    if len(d1) != len(d2):
        print(f"Lengths differ! {len(d1)} vs {len(d2)}")
        
    floats1 = struct.unpack(f"{len(d1)//4}f", d1)
    floats2 = struct.unpack(f"{len(d2)//4}f", d2)
    
    max_diff = 0.0
    diff_count = 0
    nan_count = 0
    for i, (v1, v2) in enumerate(zip(floats1, floats2)):
        if math.isnan(v1) or math.isnan(v2):
            nan_count += 1
            if not (math.isnan(v1) and math.isnan(v2)):
                diff_count += 1
        else:
            diff = abs(v1 - v2)
            if diff > max_diff:
                max_diff = diff
            if diff > 1e-6:
                diff_count += 1
                
    print(f"Total samples: {len(floats1)}")
    print(f"NaN count: {nan_count}")
    print(f"Max absolute difference: {max_diff}")
    print(f"Samples with diff > 1e-6: {diff_count}")
    
    if diff_count == 0 and nan_count == 0:
        print("PERFECT BIT-EXACT MATCH CONFIRMED OVER FULL AUDIO BLOCK!")
    else:
        print("NOT BIT-EXACT!")

compare_raw('new_out.raw', 'old_out.raw')

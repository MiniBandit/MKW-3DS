#pragma once
#include <mkw/race_camera.hpp>
namespace mkw {
inline RaceCameraCourse staffCameraCourse(){RaceCameraCourse out;
out.cameras.push_back({5,1,0,0,2,6000,{-26289.6875f,12668.9423828125f,47994.80078125f},{-24050.0f,5050.0f,54200.0f},{-23420.0f,1570.0f,30900.0f},55.0f,45.0f,360.0f});
out.cameras.push_back({5,2,1,0,4,1,{20462.916015625f,13087.1494140625f,987.6845092773438f},{29.6783447265625f,1050.0f,21650.74609375f},{29.6783447265625f,1050.0f,21650.74609375f},50.0f,25.0f,390.0f});
out.cameras.push_back({3,3,255,20,5,1,{-4898.666015625f,13602.5615234375f,19522.51171875f},{-20.0f,15.0f,550.0f},{0.0f,-4.0f,0.0f},55.0f,35.0f,305.0f});
out.cameras.push_back({3,4,255,0,5,0,{-4961.2548828125f,1011.3646240234375f,39105.5859375f},{-92.0f,5.0f,1150.0f},{0.0f,3.0f,0.0f},35.0f,50.0f,250.0f});
out.cameras.push_back({3,5,255,0,5,0,{-9465.05859375f,8955.0615234375f,41178.55078125f},{-13.0f,35.0f,1000.0f},{13.0f,-2.0f,0.0f},55.0f,35.0f,470.0f});
out.cameras.push_back({3,6,255,0,5,0,{-16714.650390625f,7525.0615234375f,38113.38671875f},{-10.0f,5.0f,800.0f},{10.0f,0.0f,0.0f},35.0f,55.0f,330.0f});
out.cameras.push_back({3,7,255,0,4,0,{-17426.533203125f,11457.5615234375f,21365.640625f},{-100.0f,10.0f,700.0f},{10.0f,8.0f,0.0f},50.0f,65.0f,330.0f});
out.cameras.push_back({3,8,255,0,3,0,{-17368.86328125f,8405.0029296875f,13692.1513671875f},{0.0f,5.0f,1000.0f},{0.0f,-10.0f,0.0f},55.0f,65.0f,250.0f});
out.cameras.push_back({3,9,255,0,5,0,{-10326.6953125f,7854.9453125f,4118.486328125f},{90.0f,2.0f,2000.0f},{0.0f,-2.0f,0.0f},20.0f,45.0f,350.0f});
out.cameras.push_back({5,10,2,0,5,2000,{9298.71484375f,7854.94482421875f,-792.1139526367188f},{10610.0f,3385.0f,-5150.0f},{15455.0f,3220.0f,2310.0f},60.0f,45.0f,270.0f});
out.cameras.push_back({5,17,4,0,15,1,{1437.482421875f,8129.9736328125f,14662.6044921875f},{14600.0f,3015.0f,5300.0f},{14600.0f,3015.0f,5300.0f},15.0f,35.0f,260.0f});
out.cameras.push_back({4,12,255,0,7,1,{-4834.3232421875f,802.65869140625f,41811.7734375f},{-4834.0f,570.0f,21800.0f},{-4834.0f,570.0f,21800.0f},40.0f,25.0f,215.0f});
out.cameras.push_back({2,15,3,0,0,5000,{-8146.70751953125f,8181.50341796875f,43602.0859375f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f},35.0f,35.0f,290.0f});
out.cameras.push_back({5,11,5,0,7,1,{-8253.0458984375f,8401.642578125f,36222.4375f},{-5685.0f,670.0f,44840.0f},{-5685.0f,670.0f,44840.0f},45.0f,25.0f,255.0f});
out.cameras.push_back({4,255,255,0,1,1500,{-20100.0f,910.0f,40690.0f},{-20100.0f,910.0f,34000.0f},{-20100.0f,10000.0f,30000.0f},45.0f,60.0f,665.0f});
out.cameras.push_back({4,14,255,0,0,1,{-20100.0f,910.0f,40690.0f},{-20100.0f,910.0f,30000.0f},{-20100.0f,910.0f,30000.0f},45.0f,45.0f,350.0f});
out.cameras.push_back({5,0,7,0,2,500,{-17432.552734375f,7889.125f,18410.375f},{-23000.0f,1900.0f,26666.0f},{-23000.0f,4200.0f,26666.0f},45.0f,55.0f,360.0f});
out.cameras.push_back({5,18,8,0,1,500,{-19465.14453125f,3217.5f,21196.794921875f},{7200.0f,4130.0f,-930.0f},{7420.0f,5050.0f,-2700.0f},55.0f,60.0f,300.0f});
out.cameras.push_back({5,13,9,0,2,250,{-209.01766967773438f,10989.9736328125f,7146.0478515625f},{-1240.0f,5000.0f,7830.0f},{-1240.0f,10000.0f,7830.0f},55.0f,45.0f,300.0f});
{RaceCameraCourse::Route r;r.smooth=false;r.oscillating=true;
r.points.push_back({-7676.0f,5000.0f,47325.0f});r.speeds.push_back(30);
r.points.push_back({-18200.0f,2165.0f,49690.0f});r.speeds.push_back(30);
out.routes.push_back(std::move(r));}
{RaceCameraCourse::Route r;r.smooth=false;r.oscillating=true;
r.points.push_back({14630.5087890625f,3016.53857421875f,5286.03564453125f});r.speeds.push_back(40);
r.points.push_back({178.40773010253906f,1089.573486328125f,21397.087890625f});r.speeds.push_back(40);
out.routes.push_back(std::move(r));}
{RaceCameraCourse::Route r;r.smooth=false;r.oscillating=true;
r.points.push_back({11644.0f,5910.0f,-3701.0f});r.speeds.push_back(20);
r.points.push_back({16757.900390625f,5910.0f,4235.39990234375f});r.speeds.push_back(20);
out.routes.push_back(std::move(r));}
{RaceCameraCourse::Route r;r.smooth=false;r.oscillating=true;
r.points.push_back({-7796.76123046875f,630.513671875f,43653.890625f});r.speeds.push_back(45);
r.points.push_back({-8044.71435546875f,630.0f,44798.796875f});r.speeds.push_back(45);
r.points.push_back({-8446.830078125f,630.0f,45520.0390625f});r.speeds.push_back(45);
r.points.push_back({-8982.7978515625f,630.0f,46167.8828125f});r.speeds.push_back(45);
r.points.push_back({-9655.400390625f,630.0f,46736.6953125f});r.speeds.push_back(45);
r.points.push_back({-10448.1328125f,630.0f,47202.703125f});r.speeds.push_back(45);
r.points.push_back({-11312.6513671875f,630.0f,47505.7578125f});r.speeds.push_back(45);
r.points.push_back({-12192.4248046875f,630.0f,47604.0546875f});r.speeds.push_back(45);
r.points.push_back({-12958.5263671875f,630.0f,47581.3203125f});r.speeds.push_back(45);
r.points.push_back({-13865.3134765625f,630.0f,47396.953125f});r.speeds.push_back(45);
r.points.push_back({-14706.95703125f,630.0f,47014.21875f});r.speeds.push_back(45);
r.points.push_back({-15451.494140625f,630.0f,46491.8828125f});r.speeds.push_back(45);
r.points.push_back({-16052.3603515625f,630.0f,45866.7734375f});r.speeds.push_back(45);
r.points.push_back({-16519.7578125f,630.0f,45163.8984375f});r.speeds.push_back(45);
r.points.push_back({-16817.9140625f,630.0f,44401.2109375f});r.speeds.push_back(45);
r.points.push_back({-16916.7578125f,636.765625f,43629.890625f});r.speeds.push_back(45);
out.routes.push_back(std::move(r));}
{RaceCameraCourse::Route r;r.smooth=true;r.oscillating=true;
r.points.push_back({13015.501953125f,3131.37939453125f,7114.65771484375f});r.speeds.push_back(29);
r.points.push_back({9697.646484375f,2407.680908203125f,10802.9814453125f});r.speeds.push_back(30);
r.points.push_back({8955.7080078125f,2297.5654296875f,11627.763671875f});r.speeds.push_back(32);
r.points.push_back({-730.8817138671875f,825.110107421875f,22395.947265625f});r.speeds.push_back(36);
out.routes.push_back(std::move(r));}
{RaceCameraCourse::Route r;r.smooth=false;r.oscillating=true;
r.points.push_back({-4883.89111328125f,660.3363037109375f,33096.3515625f});r.speeds.push_back(8);
r.points.push_back({-5199.43115234375f,677.652587890625f,37532.359375f});r.speeds.push_back(8);
out.routes.push_back(std::move(r));}
{RaceCameraCourse::Route r;r.smooth=false;r.oscillating=true;
r.points.push_back({-962.0f,5529.125f,7650.0f});r.speeds.push_back(0);
r.points.push_back({-807.145263671875f,5529.125f,7657.53466796875f});r.speeds.push_back(0);
out.routes.push_back(std::move(r));}
{RaceCameraCourse::Route r;r.smooth=false;r.oscillating=true;
r.points.push_back({-19050.0f,1200.0f,18350.0f});r.speeds.push_back(5);
r.points.push_back({-19050.0f,3500.0f,18350.0f});r.speeds.push_back(5);
out.routes.push_back(std::move(r));}
{RaceCameraCourse::Route r;r.smooth=false;r.oscillating=true;
r.points.push_back({-660.0f,3280.0f,-570.0f});r.speeds.push_back(10);
r.points.push_back({2772.0f,2965.0f,-2895.0f});r.speeds.push_back(10);
out.routes.push_back(std::move(r));}
{RaceCameraCourse::Route r;r.smooth=true;r.oscillating=true;
r.points.push_back({5784.93115234375f,3435.0f,8737.447265625f});r.speeds.push_back(12);
r.points.push_back({6182.005859375f,3035.0f,6976.84228515625f});r.speeds.push_back(12);
r.points.push_back({6086.1455078125f,2635.0f,5283.73193359375f});r.speeds.push_back(12);
r.points.push_back({5730.0f,2235.0f,3670.0f});r.speeds.push_back(12);
out.routes.push_back(std::move(r));}
return out;}
}

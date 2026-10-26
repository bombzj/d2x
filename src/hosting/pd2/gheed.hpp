#pragma once
namespace d2x {struct ClassicData;
namespace server::merchant {struct Prepared;}
namespace hosting::pd2 {
void prepareGheedStock(const ClassicData &,server::merchant::Prepared &);
void prepareGheedPurchase(const ClassicData &,server::merchant::Prepared &);
}}

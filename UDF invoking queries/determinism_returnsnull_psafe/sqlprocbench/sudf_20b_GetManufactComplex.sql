-- derived from sqlprocbench, udf in predicate, no improv using cache;
select ws_item_sk 
from
	(select ws_item_sk, count(*) cnt from web_sales group by ws_item_sk order by cnt limit 30 )t1
where sudf_20b_GetManufactComplex(ws_item_sk) = 'oughtn st';
-- derived from sqlprocbench, udf in predicate, no improv using cache;
select ws_item_sk
from
	(select ws_item_sk, count(*) cnt from web_sales group by ws_item_sk order by cnt limit 30 )t1
where sudf_20b_GetManufactComplex(ws_item_sk) is not null and sudf_20b_GetManufactComplex(ws_item_sk) = 'oughtn st';
-- derived from sqlprocbench, udf in both predicate and projection, no improv using cache;
select ws_item_sk, sudf_20b_GetManufactComplex(ws_item_sk)
from
	(select ws_item_sk, count(*) cnt from web_sales group by ws_item_sk order by cnt limit 30 )t1
where sudf_20b_GetManufactComplex(ws_item_sk) != 'outdated item';
-- derived from sqlprocbench, udf in both predicate and projection, no improv using cache;
select ws_item_sk, sudf_20b_GetManufactComplex(ws_item_sk)
from
	(select ws_item_sk, count(*) cnt from web_sales group by ws_item_sk order by cnt limit 30 )t1
where sudf_20b_GetManufactComplex(ws_item_sk) in ('outdated item', 'oughtn st');

-- derived from sqlprocbench, udf in predicate, no difference using cache;
select ws_item_sk 
from
	(select ws_item_sk, count(*) cnt from web_sales group by ws_item_sk order by cnt limit 30 )t1
where sudf_20a_GetManufactSimple(ws_item_sk) = 'oughtn st';
-- derived from sqlprocbench, udf in predicate, no difference using cache;
select ws_item_sk
from
	(select ws_item_sk, count(*) cnt from web_sales group by ws_item_sk order by cnt limit 30 )t1
where sudf_20a_GetManufactSimple(ws_item_sk) is not null and sudf_20a_GetManufactSimple(ws_item_sk) = 'oughtn st';
-- derived from sqlprocbench, udf in both predicate and projection, no improv using cache;
select ws_item_sk, sudf_20a_GetManufactSimple(ws_item_sk)
from
	(select ws_item_sk, count(*) cnt from web_sales group by ws_item_sk order by cnt limit 30 )t1
where sudf_20a_GetManufactSimple(ws_item_sk) != 'outdated item';
-- derived from sqlprocbench, udf in both predicate and projection, no improv using cache;
select ws_item_sk, sudf_20a_GetManufactSimple(ws_item_sk)
from
	(select ws_item_sk, count(*) cnt from web_sales group by ws_item_sk order by cnt limit 30 )t1
where sudf_20a_GetManufactSimple(ws_item_sk) in ('outdated item', 'oughtn st');
-- derived from q92, udf in predicate, big improv using cache;
SELECT 
         Sum(ws_ext_discount_amt)
FROM     web_sales , 
         item , 
         date_dim 
WHERE    i_manufact_id = 718 
AND      i_item_sk = ws_item_sk 
AND      d_date BETWEEN '2002-03-29' AND      ( 
                  Cast('2002-03-29' AS DATE) +  INTERVAL '90' day) 
AND      d_date_sk = ws_sold_date_sk 
AND      ws_ext_discount_amt > 
         ( 
                SELECT 1.3 * avg(ws_ext_discount_amt) 
                FROM   web_sales , 
                       date_dim 
                WHERE  ws_item_sk = i_item_sk 
                AND    d_date BETWEEN '2002-03-29' AND    ( 
                              cast('2002-03-29' AS date) + INTERVAL '90' day) 
                AND    d_date_sk = ws_sold_date_sk AND sudf_20a_GetManufactSimple(ws_item_sk) = 'oughtn st') 
ORDER BY sum(ws_ext_discount_amt) 
LIMIT 100; 





-- derived from q75, udf in predicate, big improv using cache;
WITH all_sales 
     AS (SELECT d_year, 
                i_brand_id, 
                i_class_id, 
                i_category_id, 
                i_manufact_id, 
                Sum(sales_cnt) AS sales_cnt, 
                Sum(sales_amt) AS sales_amt 
         FROM   (SELECT d_year, 
                        i_brand_id, 
                        i_class_id, 
                        i_category_id, 
                        i_manufact_id, 
                        cs_quantity - COALESCE(cr_return_quantity, 0)        AS 
                        sales_cnt, 
                        cs_ext_sales_price - COALESCE(cr_return_amount, 0.0) AS 
                        sales_amt 
                 FROM   catalog_sales 
                        JOIN item 
                          ON i_item_sk = cs_item_sk 
                        JOIN date_dim 
                          ON d_date_sk = cs_sold_date_sk 
                        LEFT JOIN catalog_returns 
                               ON ( cs_order_number = cr_order_number 
                                    AND cs_item_sk = cr_item_sk ) 
                 WHERE  i_category = 'Men' 
                 UNION 
                 SELECT d_year, 
                        i_brand_id, 
                        i_class_id, 
                        i_category_id, 
                        i_manufact_id, 
                        ss_quantity - COALESCE(sr_return_quantity, 0)     AS 
                        sales_cnt, 
                        ss_ext_sales_price - COALESCE(sr_return_amt, 0.0) AS 
                        sales_amt 
                 FROM   store_sales 
                        JOIN item 
                          ON i_item_sk = ss_item_sk 
                        JOIN date_dim 
                          ON d_date_sk = ss_sold_date_sk 
                        LEFT JOIN store_returns 
                               ON ( ss_ticket_number = sr_ticket_number 
                                    AND ss_item_sk = sr_item_sk ) 
                 WHERE  i_category = 'Men' 
                 UNION 
                 SELECT d_year, 
                        i_brand_id, 
                        i_class_id, 
                        i_category_id, 
                        i_manufact_id, 
                        ws_quantity - COALESCE(wr_return_quantity, 0)     AS 
                        sales_cnt, 
                        ws_ext_sales_price - COALESCE(wr_return_amt, 0.0) AS 
                        sales_amt 
                 FROM   web_sales 
                        JOIN item 
                          ON i_item_sk = ws_item_sk 
                        JOIN date_dim 
                          ON d_date_sk = ws_sold_date_sk 
                        LEFT JOIN web_returns 
                               ON ( ws_order_number = wr_order_number 
                                    AND ws_item_sk = wr_item_sk ) 
                 WHERE  i_category = 'Men' AND sudf_20a_GetManufactSimple(ws_item_sk) = 'oughtn st') sales_detail 
         GROUP  BY d_year, 
                   i_brand_id, 
                   i_class_id, 
                   i_category_id, 
                   i_manufact_id) 
SELECT prev_yr.d_year                        AS prev_year, 
               curr_yr.d_year                        AS year1, 
               curr_yr.i_brand_id, 
               curr_yr.i_class_id, 
               curr_yr.i_category_id, 
               curr_yr.i_manufact_id, 
               prev_yr.sales_cnt                     AS prev_yr_cnt, 
               curr_yr.sales_cnt                     AS curr_yr_cnt, 
               curr_yr.sales_cnt - prev_yr.sales_cnt AS sales_cnt_diff, 
               curr_yr.sales_amt - prev_yr.sales_amt AS sales_amt_diff 
FROM   all_sales curr_yr, 
       all_sales prev_yr 
WHERE  curr_yr.i_brand_id = prev_yr.i_brand_id 
       AND curr_yr.i_class_id = prev_yr.i_class_id 
       AND curr_yr.i_category_id = prev_yr.i_category_id 
       AND curr_yr.i_manufact_id = prev_yr.i_manufact_id 
       AND curr_yr.d_year = 2002 
       AND prev_yr.d_year = 2002 - 1 
       AND Cast(curr_yr.sales_cnt AS DECIMAL(17, 2)) / Cast(prev_yr.sales_cnt AS 
                                                                DECIMAL(17, 2)) 
           < 0.9 
ORDER  BY sales_cnt_diff
LIMIT 100; 
-- derived from q92, udf in predicate, improv using cache;
SELECT 
         Sum(ws_ext_discount_amt)
FROM     web_sales , 
         item , 
         date_dim 
WHERE    i_manufact_id = 718 
AND      i_item_sk = ws_item_sk 
AND      sudf_nmonthsbefore(d_Date_string, 3) between '2000-05-04' and '2030-10-05'
AND      d_date_sk = ws_sold_date_sk 
AND      ws_ext_discount_amt > 
         ( 
                SELECT 1.3 * avg(ws_ext_discount_amt) 
                FROM   web_sales , 
                       date_dim 
                WHERE  ws_item_sk = i_item_sk 
                AND    sudf_nmonthsbefore(d_Date_string, 3) between '1981-08-09' and '2002-03-04'
                AND    d_date_sk = ws_sold_date_sk) 
ORDER BY sum(ws_ext_discount_amt) 
LIMIT 100; 

-- derived from q92, udf in predicate, no improv using cache;
SELECT 
         Sum(ws_ext_discount_amt)
FROM     web_sales , 
         item , 
         date_dim 
WHERE    i_manufact_id = 718 
AND      i_item_sk = ws_item_sk 
AND      sudf_nmonthsbefore(d_Date_string,6) between '2001-03-04' and '2002-03-04'
AND      d_date_sk = ws_sold_date_sk 
AND      ws_ext_discount_amt > 
         ( 
                SELECT 1.3 * avg(ws_ext_discount_amt) 
                FROM   web_sales , 
                       date_dim 
                WHERE  ws_item_sk = i_item_sk 
                AND    d_date_sk = ws_sold_date_sk) 
ORDER BY sum(ws_ext_discount_amt) 
LIMIT 100; 

-- derived from q92, udf in predicate and projection, improv using cache;
SELECT 
         Sum(ws_ext_discount_amt), sudf_nmonthsbefore(d_Date_string, 8)
FROM     web_sales , 
         item , 
         date_dim 
WHERE    i_manufact_id = 718 
AND      i_item_sk = ws_item_sk 
AND      sudf_nmonthsbefore(d_Date_string, 8) between '1995-03-04' and '2002-03-04'
AND      d_date_sk = ws_sold_date_sk 
AND      ws_ext_discount_amt > 
         ( 
                SELECT 1.3 * avg(ws_ext_discount_amt) 
                FROM   web_sales , 
                       date_dim 
                WHERE  ws_item_sk = i_item_sk AND    
                sudf_nmonthsbefore(d_Date_string, 8) between '1901-03-04' and '2002-03-04'
                AND    d_date_sk = ws_sold_date_sk)
GROUP BY sudf_nmonthsbefore(d_Date_string, 8) 
ORDER BY sum(ws_ext_discount_amt) 
LIMIT 100; 

-- derived from q92, udf in predicate, no improv using cache;
SELECT 
         Sum(ws_ext_discount_amt)
FROM     web_sales , 
         item , 
         date_dim 
WHERE    i_manufact_id = 718 
AND      i_item_sk = ws_item_sk 
AND      sudf_nmonthsbefore(i_rec_start_date_string,5) between '1990-03-04' and '2002-03-04'
AND      d_date_sk = ws_sold_date_sk 
AND      ws_ext_discount_amt > 
         ( 
                SELECT 1.3 * avg(ws_ext_discount_amt) 
                FROM   web_sales , 
                       date_dim 
                WHERE  ws_item_sk = i_item_sk AND    
                sudf_nmonthsbefore(i_rec_end_date_string,5) between '1980-03-04' and '2002-03-04'
                AND    d_date_sk = ws_sold_date_sk)
ORDER BY sum(ws_ext_discount_amt) 
LIMIT 100; 

-- derived from q92, udf in predicate, improv using cache;
SELECT 
         Sum(ws_ext_discount_amt)
FROM     web_sales , 
         item , 
         date_dim 
WHERE    i_manufact_id = 718 
AND      i_item_sk = ws_item_sk 
AND      sudf_nmonthsbefore(i_rec_start_date_string,1) between '2000-03-04' and '2002-03-04'
AND      d_date_sk = ws_sold_date_sk 
AND      ws_ext_discount_amt > 
         ( 
                SELECT 1.3 * avg(ws_ext_discount_amt) 
                FROM   web_sales , 
                       date_dim 
                WHERE  ws_item_sk = i_item_sk
                AND    d_date_sk = ws_sold_date_sk)
ORDER BY sum(ws_ext_discount_amt) 
LIMIT 100; 


-- derived from q67, udf in predicate, no improv using cache; 
select *
from (select i_category
            ,i_class
            ,i_brand
            ,i_product_name
            ,d_year
            ,d_qoy
            ,d_moy
            ,s_store_id
            ,sumsales
            ,rank() over (partition by i_category order by sumsales desc) rk
      from (select i_category
                  ,i_class
                  ,i_brand
                  ,i_product_name
                  ,d_year
                  ,d_qoy
                  ,d_moy
                  ,s_store_id
                  ,sum(coalesce(ss_sales_price*ss_quantity,0)) sumsales
            from store_sales
                ,date_dim
                ,store
                ,item
       where  ss_sold_date_sk=d_date_sk
          and ss_item_sk=i_item_sk
          and ss_store_sk = s_store_sk
          and d_month_seq between 1181 and 1181+11 and sudf_nmonthsbefore(s_rec_start_date_string, 4) between '1900-10-04' and '2008-03-04'
       group by  rollup(i_category, i_class, i_brand, i_product_name, d_year, d_qoy, d_moy,s_store_id))dw1) dw2
where rk <= 100
order by i_category
        ,i_class
        ,i_brand
        ,i_product_name
        ,d_year
        ,d_qoy
        ,d_moy
        ,s_store_id
        ,sumsales
        ,rk
LIMIT 100; 

-- derived from q67, udf in predicate, no improv using cache;
select *
from (select i_category
            ,i_class
            ,i_brand
            ,i_product_name
            ,d_year
            ,d_qoy
            ,d_moy
            ,s_store_id
            ,sumsales
            ,rank() over (partition by i_category order by sumsales desc) rk
      from (select i_category
                  ,i_class
                  ,i_brand
                  ,i_product_name
                  ,d_year
                  ,d_qoy
                  ,d_moy
                  ,s_store_id
                  ,sum(coalesce(ss_sales_price*ss_quantity,0)) sumsales
            from store_sales
                ,date_dim
                ,store
                ,item
       where  ss_sold_date_sk=d_date_sk
          and ss_item_sk=i_item_sk
          and ss_store_sk = s_store_sk
          and d_month_seq between 1181 and 1181+11 and sudf_nmonthsbefore(s_rec_start_date_string, 3) between '1900-10-04' and '2008-03-04' and sudf_nmonthsbefore(s_rec_end_date_string, 3) between '2008-03-04' and '2028-03-04'
       group by  rollup(i_category, i_class, i_brand, i_product_name, d_year, d_qoy, d_moy,s_store_id))dw1) dw2
where rk <= 100
order by i_category
        ,i_class
        ,i_brand
        ,i_product_name
        ,d_year
        ,d_qoy
        ,d_moy
        ,s_store_id
        ,sumsales
        ,rk
LIMIT 100; 




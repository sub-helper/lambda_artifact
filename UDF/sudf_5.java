package org.example.pgsql_udf;

import static org.postgresql.pljava.annotation.Function.Effects;
import static org.postgresql.pljava.annotation.Function.OnNullInput;
import static org.postgresql.pljava.annotation.Function.Parallel;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.PrintStream;
import java.lang.annotation.RetentionPolicy;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Random;
import java.util.Set;
import org.postgresql.pljava.annotation.Function;
import org.postgresql.pljava.annotation.Function.Effects;

public class sudf_5 {

  private static String m_url = "jdbc:default:connection";
  private static String remove_trailingspace_regex = "\\s+$";
  private static HashMap<Integer, Double> cache = new HashMap<Integer, Double>();

  public static Double evaluation(Integer dep) throws SQLException {
    Integer morningSale = null;
    Integer eveningSale = null;
    String query1 =
      "select  count(*) from web_sales_history, time_dim, customer_demographics where ws_sold_time_sk = t_time_sk and ws_bill_customer_sk = cd_demo_sk and t_hour>=8 and t_hour<=9 and cd_dep_count=?";
    String query2 =
      "select  count(*) from web_sales_history, time_dim, customer_demographics where ws_sold_time_sk = t_time_sk and ws_bill_customer_sk = cd_demo_sk and t_hour>=19 and t_hour<=20 and cd_dep_count=?";

    Connection conn = DriverManager.getConnection(m_url);
    PreparedStatement stmt1 = conn.prepareStatement(query1);
    PreparedStatement stmt2 = conn.prepareStatement(query2);
    stmt1.setInt(1, dep);
    stmt2.setInt(1, dep);
    ResultSet rs1 = stmt1.executeQuery();
    if (rs1.next()) {
      morningSale = rs1.getInt(1);
      if (morningSale == null) {
        morningSale = Integer.valueOf(0);
      }
    }
    ResultSet rs2 = stmt2.executeQuery();
    if (rs2.next()) {
      eveningSale = rs2.getInt(1);
      if (eveningSale == null) {
        eveningSale = Integer.valueOf(0);
      }
    }

    stmt1.close();
    stmt2.close();

    conn.close();
    return Double.valueOf(morningSale / eveningSale);
  }

  @Function
  public static Double sudf_5_morEveRatio(Integer dep) throws SQLException {
    if (dep == null) {
      return null;
    }
    Double result = evaluation(dep);
    return result;
  }
  @Function(onNullInput = OnNullInput.RETURNS_NULL)
  public static Double sudf_5_morEveRatio_null(Integer dep) throws SQLException {
    if (dep == null) {
      return null;
    }
    Double result = evaluation(dep);
    return result;
  }
  @Function(parallel = Parallel.SAFE)
  public static Double sudf_5_morEveRatio_safe(Integer dep) throws SQLException {
    if (dep == null) {
      return null;
    }
    Double result = evaluation(dep);
    return result;
  }

  @Function
  public static Double sudf_5_morEveRatio_cache(Integer key)
    throws SQLException {
    if (key == null) {
      return null;
    }
    if (cache.containsKey(key)) {
      return cache.get(key);
    } else {
      Double result = evaluation(key);
      cache.put(key, result);
      return result;
    }
  }
}

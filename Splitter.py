import pandas as pd
import requests

def process_dataframe_optimized(df, base_url="http://localhost:8000"):
    # 1. Générer les pv_stat_id (rs_request)
    df["rs_request"] = df.apply(
        lambda row: [
            f"pvstats#var#sophis#{product}#pricing#{row['cob_date']}"
            for product in row["products"]
        ],
        axis=1
    )

    # 2. Exploser les lignes
    df_exploded = df.explode("rs_request", ignore_index=True)
    df_exploded.rename(columns={"rs_request": "pv_stat_id"}, inplace=True)

    # 3. Répartition du coût (proportionnelle au nombre de produits)
    df_exploded["task_cost_split"] = df_exploded.groupby("id")["task_cost"].transform(
        lambda x: x / x.count()
    )

    # 4. Appel API : corps = liste simple
    unique_requests = df_exploded["pv_stat_id"].unique().tolist()
    try:
        response = requests.post(
            f"{base_url}/pvstats",
            json=unique_requests  # corps = liste directe
        )
        response.raise_for_status()
        rest_response = response.json()
    except requests.RequestException as e:
        raise RuntimeError(f"Erreur API /pvstats : {e}")

    # 5. Mapping taskPlCalcTime
    mapping = {k: v.get("taskPlCalcTime", None) for k, v in rest_response.items()}
    df_exploded["taskPlCalcTime"] = df_exploded["pv_stat_id"].map(mapping)

    return df_exploded.reset_index(drop=True)
